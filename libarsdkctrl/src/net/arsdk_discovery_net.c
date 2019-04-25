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

#include "arsdkctrl_priv.h"
#include <net/arsdk_net.h>
#include "arsdkctrl_net_log.h"

/** */
struct arsdk_discovery_net {
	struct arsdk_discovery          *parent;
	struct arsdkctrl_backend_net    *backend;
	struct pomp_ctx                 *ctx;
	char                            *addr;
	enum arsdk_device_type          *types;
	size_t                          n_types;
	struct arsdk_discovery_device_info dev_info;
};

static void event_cb(struct pomp_ctx *ctx,
		enum pomp_event event,
		struct pomp_conn *conn,
		const struct pomp_msg *msg,
		void *userdata)
{
	struct arsdk_discovery_net *self = userdata;

	if (event == POMP_EVENT_DISCONNECTED) {
		arsdk_discovery_remove_device(self->parent, &self->dev_info);

		/* Reset device info */
		free((char *)self->dev_info.name);
		self->dev_info.name = NULL;
		free((char *)self->dev_info.id);
		self->dev_info.id = NULL;
	}
}

static int is_devtype_supported(struct arsdk_discovery_net *self,
		enum arsdk_device_type type)
{
	uint32_t i;
	for (i = 0; i < self->n_types; i++) {
		if (self->types[i] == type)
			return 1;
	}

	return 0;
}

static int json_parsing(const void *cdata, size_t len,
		struct arsdk_discovery_device_info *info) {

	int res = 0;
	json_object *jsonObj = NULL;
	json_object *jtype = NULL;
	const char *typeStr = NULL;
	json_object *jid = NULL;
	json_object *jname = NULL;
	json_object *jport = NULL;

	jsonObj = json_tokener_parse(cdata);
	if (jsonObj == NULL)
		return -EINVAL;

	jtype = get_json_object(jsonObj, ARSDK_NET_DISCOVERY_KEY_TYPE);
	if (jtype == NULL) {
		res = -EINVAL;
		goto error;
	}
	typeStr = json_object_get_string(jtype);
	info->type = strtol(typeStr, NULL, 16);

	jid = get_json_object(jsonObj, ARSDK_NET_DISCOVERY_KEY_ID);
	if (jid == NULL) {
		res = -EINVAL;
		goto error;
	}
	info->id = xstrdup(json_object_get_string(jid));

	jname = get_json_object(jsonObj, ARSDK_NET_DISCOVERY_KEY_NAME);
	if (jname == NULL) {
		res = -EINVAL;
		goto error;
	}
	info->name = xstrdup(json_object_get_string(jname));

	jport = get_json_object(jsonObj, ARSDK_NET_DISCOVERY_KEY_PORT);
	if (jport == NULL) {
		res = -EINVAL;
		goto error;
	}
	info->port = json_object_get_int(jport);

	return 0;

	/* Cleanup in case of error */
error:
	json_object_put(jsonObj);
	return res;
}

static void raw_cb(struct pomp_ctx *ctx, struct pomp_conn *conn,
		   struct pomp_buffer *buf, void *userdata)
{
	struct arsdk_discovery_net *self = userdata;
	const void *cdata = NULL;
	size_t len = 0;
	int res = 0;

	ARSDK_RETURN_IF_FAILED(self != NULL, -EINVAL);

	/* Number of device is limited to one */
	if (self->dev_info.name != NULL)
		return;

	/* Get data from buffer */
	res = pomp_buffer_get_cdata(buf, &cdata, &len, NULL);
	if (res < 0) {
		ARSDK_LOG_ERRNO("pomp_buffer_get_cdata", -res);
		return;
	}

	/* setup device info */
	res = json_parsing(cdata, len, &self->dev_info);
	if (res < 0)
		return;

	/* add device if type is supported */
	if (is_devtype_supported(self, self->dev_info.type))
		arsdk_discovery_add_device(self->parent, &self->dev_info);
}

/**
 */
static void socket_cb(struct pomp_ctx *ctx,
		int fd,
		enum pomp_socket_kind kind,
		void *userdata)
{
	struct arsdk_discovery_net *self = userdata;
	struct arsdkctrl_backend *base;

	ARSDK_RETURN_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(self->backend != NULL, -EINVAL);

	/* socket hook callback */
	base = arsdkctrl_backend_net_get_parent(self->backend);

	arsdkctrl_backend_socket_cb(base, fd, ARSDK_SOCKET_KIND_DISCOVERY);
}

/**
 */
int arsdk_discovery_net_new(struct arsdk_ctrl *ctrl,
		struct arsdkctrl_backend_net *backend,
		const struct arsdk_discovery_cfg *cfg,
		const char *addr,
		struct arsdk_discovery_net **ret_obj)
{
	struct arsdk_discovery_net *self = NULL;
	int res = 0;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(backend != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ctrl != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg->types != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg->count > 0, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(addr != NULL, -EINVAL);

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Initialize structure */
	self->backend = backend;
	self->ctx = pomp_ctx_new_with_loop(&event_cb, self,
			arsdk_ctrl_get_loop(ctrl));
	self->addr = xstrdup(addr);
	self->dev_info.addr = self->addr;

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

	/* Copy discovery config */
	self->types = calloc(cfg->count, sizeof(enum arsdk_device_type));
	if (!self->types) {
		res = -ENOMEM;
		goto error;
	}

	self->n_types = cfg->count;
	memcpy(self->types, cfg->types,
		cfg->count * sizeof(enum arsdk_device_type));

	/* create discovery */
	res = arsdk_discovery_new("net",
			arsdkctrl_backend_net_get_parent(backend), ctrl,
			&self->parent);
	if (res < 0) {
		ARSDK_LOG_ERRNO("arsdk_discovery_new", -res);
		goto error;
	}

	*ret_obj = self;
	return 0;

	/* Cleanup in case of error */
error:
	arsdk_discovery_net_destroy(self);
	return res;
}

/**
 */
int arsdk_discovery_net_destroy(struct arsdk_discovery_net *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	pomp_ctx_destroy(self->ctx);

	arsdk_discovery_destroy(self->parent);
	self->parent = NULL;

	free(self->types);
	free(self->addr);

	/* Free resources */
	free(self);
	return 0;
}

/**
 */
int arsdk_discovery_net_start(struct arsdk_discovery_net *self)
{
	struct sockaddr_in addr;
	int res = 0;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	/* Start discovery */
	res = arsdk_discovery_start(self->parent);
	if (res < 0) {
		ARSDK_LOG_ERRNO("arsdk_discovery_start", -res);
		return res;
	}

	addr.sin_port = htons(ARSDK_NET_DISCOVERY_PORT);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(self->addr);

	res = pomp_ctx_connect(self->ctx, (struct sockaddr *)&addr,
			sizeof(addr));
	if (res < 0) {
		ARSDK_LOG_ERRNO("pomp_ctx_connect", -res);
		goto error;
	}

	return 0;

	/* Cleanup in case of error */
error:
	arsdk_discovery_net_stop(self);
	return res;
}

/**
 */
int arsdk_discovery_net_stop(struct arsdk_discovery_net *self)
{
	int res = 0;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	res = pomp_ctx_stop(self->ctx);
	if (res < 0) {
		ARSDK_LOG_ERRNO("pomp_ctx_connect", -res);
		return res;
	}

	arsdk_discovery_stop(self->parent);

	return res;
}
