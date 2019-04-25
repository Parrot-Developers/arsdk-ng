/**
 * Copyright (c) 2019 Parrot Drones SAS
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Parrot Drones SAS Company nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE PARROT DRONES SAS COMPANY BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "arsdk_priv.h"
#include "arsdk_net.h"
#include "arsdk_net_log.h"

/** */
struct arsdk_publisher_net {
	struct arsdk_backend_net  *backend;

	struct pomp_ctx *ctx;
	struct pomp_buffer *buf;
	int stop;
	struct sockaddr addr;
};

static void event_cb(struct pomp_ctx *ctx, enum pomp_event event,
		     struct pomp_conn *conn, const struct pomp_msg *msg,
		     void *user_data)
{
	struct arsdk_publisher_net *self = user_data;
	int ret;

	/* only handle connected event */
	if (event != POMP_EVENT_CONNECTED)
		return;

	/* send payload */
	ret = pomp_conn_send_raw_buf(conn, self->buf);
	if (ret < 0)
		ARSDK_LOGE("can't send buffer: %s", strerror(-ret));
}

static void raw_cb(struct pomp_ctx *ctx, struct pomp_conn *conn,
		   struct pomp_buffer *buf, void *userdata)
{

}

static int get_ip_addr(struct in_addr *addr, const char *interface_name)
{
#if defined(__linux__) && !defined(ANDROID)
	int ret = -1;
	struct sockaddr_in *addr_p;
	struct ifaddrs *addrs;
	struct ifaddrs *tmp;
	char buffer[INET_ADDRSTRLEN];

	if (getifaddrs(&addrs) < 0) {
		ARSDK_LOGE("Cannot get interfaces list");
		return -1;
	}

	for (tmp = addrs; tmp != NULL; tmp = tmp->ifa_next) {
		if (tmp->ifa_name == NULL || tmp->ifa_addr == NULL)
			continue;
		if (strcmp(tmp->ifa_name, interface_name))
			continue;
		if (tmp->ifa_addr->sa_family == AF_INET) {
			addr_p = (struct sockaddr_in *)tmp->ifa_addr;
			*addr = addr_p->sin_addr;
			if (inet_ntop(AF_INET, addr, buffer,
					sizeof(buffer)) == NULL)
				continue;
			ARSDK_LOGI("address: %s", buffer);
			ret = 0;
			goto out;
		}
	}
	ARSDK_LOGE("Cannot find interface %s", interface_name);

out:
	freeifaddrs(addrs);

	return ret;
#else /*  defined(__linux__) && !defined(ANDROID) */
	return -ENOSYS;
#endif /* defined(__linux__) && !defined(ANDROID) */
}

/**
 */
static void socket_cb(struct pomp_ctx *ctx,
		int fd,
		enum pomp_socket_kind kind,
		void *userdata)
{
	struct arsdk_publisher_net *self = userdata;
	struct arsdk_backend *base;

	ARSDK_RETURN_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(self->backend != NULL, -EINVAL);

	/* socket hook callback */
	base = arsdk_backend_net_get_parent(self->backend);

	arsdk_backend_socket_cb(base, fd, ARSDK_SOCKET_KIND_DISCOVERY);
}

/**
 */
int arsdk_publisher_net_new(
		struct arsdk_backend_net *backend,
		struct pomp_loop *loop,
		const char *interface_name,
		struct arsdk_publisher_net **ret_obj)
{
	int res = 0;
	struct arsdk_publisher_net *self = NULL;
	struct sockaddr_in *addr = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(backend != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(loop != NULL, -EINVAL);

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Initialize structure */
	self->backend = backend;

	/* create context */
	self->ctx = pomp_ctx_new_with_loop(&event_cb,
			self, loop);
	if (!self->ctx) {
		res = -ENOMEM;
		goto error;
	}

	/* Set socket callback*/
	res = pomp_ctx_set_socket_cb(self->ctx, socket_cb);
	if (res < 0) {
		ARSDK_LOG_ERRNO("pomp_ctx_set_socket_cb", -res);
		goto error;
	}

	/* use pomp in raw mode */
	res = pomp_ctx_set_raw(self->ctx, &raw_cb);
	if (res < 0) {
		ARSDK_LOG_ERRNO("pomp_ctx_set_raw", -res);
		goto error;
	}

	/* Disable TCP keepalive */
	res = pomp_ctx_setup_keepalive(self->ctx, 0, 0, 0, 0);
	if (res < 0) {
		ARSDK_LOG_ERRNO("pomp_ctx_setup_keepalive", -res);
		goto error;
	}

	addr = (struct sockaddr_in *)&self->addr;
	addr->sin_port = htons(ARSDK_NET_DISCOVERY_PORT);
	if (interface_name == NULL)
		addr->sin_addr.s_addr = htonl(INADDR_ANY);
	else
		if (get_ip_addr(&addr->sin_addr, interface_name) < 0)
			goto error;
	addr->sin_family = AF_INET;

	*ret_obj = self;
	return 0;

	/* Cleanup in case of error */
error:
	arsdk_publisher_net_destroy(self);
	return res;
}

/**
 */
int arsdk_publisher_net_destroy(struct arsdk_publisher_net *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	pomp_ctx_destroy(self->ctx);

	free(self);
	return 0;
}

/**
 */
int arsdk_publisher_net_start(
		struct arsdk_publisher_net *self,
		const struct arsdk_publisher_net_cfg *cfg)
{
	int res = 0;
	char payload[255];

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg->base.id != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg->base.id[0] != '\0', -EINVAL);

	/* format payload & create buffer */
	res = snprintf(payload, sizeof(payload), "{ \"%s\": \"0x%04x\","
			"\"%s\": \"%s\", "
			"\"%s\": %u,"
			"\"%s\": \"%s\"}",
			ARSDK_NET_DISCOVERY_KEY_TYPE, cfg->base.type,
			ARSDK_NET_DISCOVERY_KEY_ID, cfg->base.id,
			ARSDK_NET_DISCOVERY_KEY_PORT, cfg->port,
			ARSDK_NET_DISCOVERY_KEY_NAME, cfg->base.name);

	self->buf = pomp_buffer_new_with_data(payload, res + 1);
	if (!self->buf)
		return -ENOMEM;

	res = pomp_ctx_listen(self->ctx, &self->addr, sizeof(self->addr));
	if (res < 0) {
		ARSDK_LOGE("can't listen: %m");
		return res;
	}

	ARSDK_LOGI("publish on port %d: %s", ARSDK_NET_DISCOVERY_PORT, payload);

	/* Success */
	return 0;
}

/**
 */
int arsdk_publisher_net_stop(struct arsdk_publisher_net *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	pomp_ctx_stop(self->ctx);

	if (self->buf != NULL)
		pomp_buffer_unref(self->buf);

	return 0;
}
