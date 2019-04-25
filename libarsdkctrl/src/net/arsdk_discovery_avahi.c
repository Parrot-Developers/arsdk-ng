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
#include <arsdkctrl/arsdk_discovery_avahi.h>
#include "arsdk_avahi_loop.h"
#include "arsdkctrl_net_log.h"

#ifdef BUILD_AVAHI_CLIENT
#include <avahi-common/error.h>
#include <avahi-client/client.h>
#include <avahi-client/lookup.h>

#define AVAHI_SERVICE_TYPE_FMT "_arsdk-%04x._udp"
#endif /* BUILD_AVAHI_CLIENT */

/** */
struct arsdk_discovery_avahi {
	struct arsdk_discovery          *parent;
	char                            **services;
	uint32_t                        services_count;
	struct arsdkctrl_backend_net    *backend;

#ifdef BUILD_AVAHI_CLIENT
	/* avahi_browser */
	struct arsdk_avahi_loop   *aloop;
	AvahiClient               *client;
	AvahiServiceBrowser       **service_browsers;
	uint32_t                  service_count;
#endif /* BUILD_AVAHI_CLIENT */
};

/** */
struct find_device_ctx {
	const char              *name;
	enum arsdk_device_type  type;
	struct arsdk_device     *device;
};

/**
 */
#ifdef BUILD_AVAHI_CLIENT
static enum arsdk_device_type extract_device_type(const char *type)
{
	int devtype = ARSDK_DEVICE_TYPE_UNKNOWN;
	sscanf(type, AVAHI_SERVICE_TYPE_FMT, &devtype);
	return devtype;
}
#endif /* BUILD_AVAHI_CLIENT */

/**
 */
#ifdef BUILD_AVAHI_CLIENT
static int json_extract_device_id(const char *s, char **device_id)
{
	int res = 0;
	json_object *jobj = NULL;
	json_object *jobjval = NULL;

	if (device_id == NULL)
		return -EINVAL;
	*device_id = NULL;

	jobj = json_tokener_parse(s);
	if (jobj == NULL)
		return -EINVAL;

	if (!json_object_is_type(jobj, json_type_object)) {
		res = -EINVAL;
		goto out;
	}

	jobjval = get_json_object(jobj, "device_id");
	if (jobjval == NULL) {
		res = -EINVAL;
		goto out;
	}

	if (!json_object_is_type(jobjval, json_type_string)) {
		res = -EINVAL;
		goto out;
	}

	*device_id = xstrdup(json_object_get_string(jobjval));

out:
	json_object_put(jobj);
	return res;
}
#endif /* BUILD_AVAHI_CLIENT */

/**
 */
#ifdef BUILD_AVAHI_CLIENT
static void service_added(struct arsdk_discovery_avahi *self,
		const char *name,
		const char *type,
		const char *addr,
		uint16_t port,
		const char *txtdata)
{
	struct arsdk_discovery_device_info info;
	char *id = NULL;

	memset(&info, 0, sizeof(info));

	/* Extract device type from service type */
	info.type = extract_device_type(type);
	if (info.type == ARSDK_DEVICE_TYPE_UNKNOWN) {
		ARSDK_LOGW("Unable to extract device type from '%s'", type);
		return;
	}

	/* Try to get device id from json found in txtdata */
	if (txtdata != NULL)
		json_extract_device_id(txtdata, &id);

	info.name = name;
	info.addr = addr;
	info.port = port;
	info.id = id;

	/* Create new device */
	arsdk_discovery_add_device(self->parent, &info);

	free(id);
}
#endif /* BUILD_AVAHI_CLIENT */

/**
 */
#ifdef BUILD_AVAHI_CLIENT
static void service_removed(struct arsdk_discovery_avahi *self,
		const char *name,
		const char *type)
{
	struct arsdk_discovery_device_info info;

	memset(&info, 0, sizeof(info));

	info.name = name;
	/* Extract device type from service type */
	info.type = extract_device_type(type);
	if (info.type == ARSDK_DEVICE_TYPE_UNKNOWN) {
		ARSDK_LOGW("Unable to extract device type from '%s'", type);
		return;
	}

	arsdk_discovery_remove_device(self->parent, &info);
}

/**
 */
static void socket_cb(struct arsdk_avahi_loop *loop,
		int fd,
		enum arsdk_socket_kind kind,
		void *userdata)
{
	struct arsdk_discovery_avahi *self = userdata;
	struct arsdkctrl_backend *base;

	ARSDK_RETURN_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(self->backend != NULL, -EINVAL);

	/* socket hook callback */
	base = arsdkctrl_backend_net_get_parent(self->backend);
	arsdkctrl_backend_socket_cb(base, fd, kind);
}

/**
 */
static void avahi_service_resolver_cb(AvahiServiceResolver *r,
		AvahiIfIndex interface,
		AvahiProtocol protocol,
		AvahiResolverEvent event,
		const char *name,
		const char *type,
		const char *domain,
		const char *host_name,
		const AvahiAddress *a,
		uint16_t port,
		AvahiStringList *txt,
		AvahiLookupResultFlags flags,
		void *userdata)
{
	struct arsdk_discovery_avahi *self = userdata;
	char addrstr[AVAHI_ADDRESS_STR_MAX] = "";
	char *txtdata = NULL;

	switch (event) {
	case AVAHI_RESOLVER_FOUND:
		avahi_address_snprint(addrstr, sizeof(addrstr), a);
		if (txt != NULL) {
			txtdata = calloc(1, txt->size + 1);
			if (txtdata != NULL)
				memcpy(txtdata, txt->text, txt->size);
		}
		service_added(self,
				name,
				type,
				addrstr,
				port,
				txtdata);
		if (txtdata != NULL)
			free(txtdata);
		break;

	case AVAHI_RESOLVER_FAILURE:
		ARSDK_LOGE("avahi_service_resolver_cb: FAILURE");
		break;

	default:
		break;
	}

	/* We don't need the resolver anymore
	 * NOTE: if we stop the browsing before this callback is called
	 * we might leak it */
	avahi_service_resolver_free(r);
}

/**
 */
static void avahi_service_browser_cb(AvahiServiceBrowser *b,
		AvahiIfIndex interface,
		AvahiProtocol protocol,
		AvahiBrowserEvent event,
		const char *name,
		const char *type,
		const char *domain,
		AvahiLookupResultFlags flags,
		void *userdata)
{
	struct arsdk_discovery_avahi *self = userdata;

	switch (event) {
	case AVAHI_BROWSER_NEW:
		avahi_service_resolver_new(self->client,
				interface,
				protocol,
				name,
				type,
				domain,
				AVAHI_PROTO_UNSPEC,
				0,
				&avahi_service_resolver_cb,
				self);
		break;

	case AVAHI_BROWSER_REMOVE:
		service_removed(self,
				name, type);
		break;

	case AVAHI_BROWSER_CACHE_EXHAUSTED: /* NO BREAK */
	case AVAHI_BROWSER_ALL_FOR_NOW:
		break;

	case AVAHI_BROWSER_FAILURE:
		ARSDK_LOGE("avahi_service_browser_cb: FAILURE");
		break;

	default:
		break;
	}
}

/**
 */
static void avahi_client_cb(AvahiClient *client,
		AvahiClientState state,
		void *userdata)
{
	if (state == AVAHI_CLIENT_S_RUNNING)
		ARSDK_LOGI("avahi_client_cb: S_RUNNING");
	else if (state == AVAHI_CLIENT_FAILURE)
		ARSDK_LOGE("avahi_client_cb: FAILURE");
}

/**
 */
static int avahi_browser_clean(struct arsdk_discovery_avahi *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	if (self->service_browsers != NULL)
		return -EBUSY;

	/* Free resources */
	if (self->client != NULL) {
		avahi_client_free(self->client);
		self->client = NULL;
	}

	if (self->aloop != NULL) {
		arsdk_avahi_loop_destroy(self->aloop);
		self->aloop = NULL;
	}

	return 0;
}

/**
 */
static int avahi_browser_init(struct arsdk_discovery_avahi *self,
		struct pomp_loop *ploop)
{
	int res = 0;
	struct arsdk_avahi_loop_cbs loop_cbs;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ploop != NULL, -EINVAL);

	/* Allocate avahi loop */
	memset(&loop_cbs, 0, sizeof(loop_cbs));
	loop_cbs.userdata = self;
	loop_cbs.socketcb = &socket_cb;
	res = arsdk_avahi_loop_new(ploop, &loop_cbs, &self->aloop);
	if (res < 0)
		goto error;

	/* Create avahi client */
	self->client = avahi_client_new(
			arsdk_avahi_loop_get_poll(self->aloop),
			AVAHI_CLIENT_NO_FAIL,
			&avahi_client_cb, self, &res);
	if (self->client == NULL) {
		ARSDK_LOGE("avahi_client_new: err=%d(%s)",
				res, avahi_strerror(res));
		res = -ENOMEM;
		goto error;
	}

	return 0;

	/* Cleanup in case of error */
error:
	avahi_browser_clean(self);
	return res;
}

/**
 */
static int avahi_browser_start(struct arsdk_discovery_avahi *self,
		const char * const *services,
		uint32_t count)
{
	int res = 0;
	uint32_t i = 0;
	AvahiServiceBrowser *service_browser = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(services != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(count != 0, -EINVAL);

	if (self->service_browsers != NULL)
		return -EBUSY;

	/* Allocate service browser table */
	self->service_browsers = calloc(count,
			sizeof(AvahiServiceBrowser *));
	if (self->service_browsers == NULL)
		return -ENOMEM;
	self->service_count = count;

	/* Setup avahi service browsers */
	for (i = 0; i < self->service_count; i++) {
		if (services[i] == NULL) {
			res = -EINVAL;
			goto error;
		}
		self->service_browsers[i] = avahi_service_browser_new(
				self->client,
				AVAHI_IF_UNSPEC,
				AVAHI_PROTO_UNSPEC,
				services[i],
				NULL,
				0,
				&avahi_service_browser_cb,
				self);
		if (self->service_browsers[i] == NULL) {
			res = -ENOMEM;
			ARSDK_LOGE("avahi_service_browser_new: err");
			goto error;
		}
	}

	return 0;

	/* Cleanup in case of errors */
error:
	for (i = 0; i < self->service_count; i++) {
		service_browser = self->service_browsers[i];
		if (service_browser != NULL)
			avahi_service_browser_free(service_browser);
	}
	free(self->service_browsers);
	self->service_browsers = NULL;
	self->service_count = 0;
	return res;
}

/**
 */
static int avahi_browser_stop(struct arsdk_discovery_avahi *self)
{
	uint32_t i = 0;
	AvahiServiceBrowser *service_browser = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	if (self->service_browsers == NULL)
		return 0;

	/* Free avahi service browsers */
	for (i = 0; i < self->service_count; i++) {
		service_browser = self->service_browsers[i];
		if (service_browser != NULL)
			avahi_service_browser_free(service_browser);
	}
	free(self->service_browsers);
	self->service_browsers = NULL;
	self->service_count = 0;

	return 0;
}

#endif /* BUILD_AVAHI_CLIENT */

/**
 */
int arsdk_discovery_avahi_new(struct arsdk_ctrl *ctrl,
		struct arsdkctrl_backend_net *backend,
		const struct arsdk_discovery_cfg *cfg,
		struct arsdk_discovery_avahi **ret_obj)
{
	struct arsdk_discovery_avahi *self = NULL;
	int res = 0;
#ifdef BUILD_AVAHI_CLIENT
	uint32_t i;
	char type[256] = "";
#endif /* BUILD_AVAHI_CLIENT */

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(backend != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ctrl != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg->types != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg->count > 0, -EINVAL);

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Initialize structure */
	self->backend = backend;

	/* create discovery */
	res = arsdk_discovery_new("avahi",
			arsdkctrl_backend_net_get_parent(backend), ctrl,
			&self->parent);
	if (res) {
		ARSDK_LOG_ERRNO("arsdk_discovery_new", -res);
		goto error;
	}

#ifdef BUILD_AVAHI_CLIENT
	/* Create avahi browser */
	res = avahi_browser_init(self, arsdk_ctrl_get_loop(ctrl));
	if (res < 0) {
		ARSDK_LOG_ERRNO("avahi_browser_new", -res);
		goto error;
	}

	/* Construct the list of services */
	self->services_count = cfg->count;
	self->services = calloc(cfg->count, sizeof(char *));
	if (self->services == NULL) {
		res = -ENOMEM;
		goto error;
	}

	for (i = 0; i < cfg->count; i++) {
		/* Not using asprintf for portability reasons */
		snprintf(type, sizeof(type), AVAHI_SERVICE_TYPE_FMT,
				cfg->types[i]);
		self->services[i] = strdup(type);
		if (self->services[i] == NULL) {
			res = -ENOMEM;
			goto error;
		}
	}

#endif /* BUILD_AVAHI_CLIENT */

	*ret_obj = self;
	return 0;

	/* Cleanup in case of error */
error:
	arsdk_discovery_avahi_destroy(self);
	return res;
}

/**
 */
int arsdk_discovery_avahi_destroy(struct arsdk_discovery_avahi *self)
{
#ifdef BUILD_AVAHI_CLIENT
	uint32_t i;
	int res = 0;
#endif /* BUILD_AVAHI_CLIENT*/
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

#ifdef BUILD_AVAHI_CLIENT
	/* Free avahi browser */
	if (self != NULL) {
		res = avahi_browser_clean(self);
		if (res < 0)
			ARSDK_LOG_ERRNO("avahi_browser_destroy", -res);
	}

	if (self->services != NULL) {
		for (i = 0; i < self->services_count; i++)
			free(self->services[i]);
		free(self->services);
	}
#endif /* BUILD_AVAHI_CLIENT*/

	arsdk_discovery_destroy(self->parent);
	self->parent = NULL;

	/* Free resources */
	free(self);
	return 0;
}

/**
 */
int arsdk_discovery_avahi_start(struct arsdk_discovery_avahi *self)
{
#ifdef BUILD_AVAHI_CLIENT
	int res = 0;
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	/* start discovery */
	res = arsdk_discovery_start(self->parent);
	if (res < 0) {
		ARSDK_LOG_ERRNO("arsdk_discovery_start", -res);
		return res;
	}

	/* Start avahi discovery */
	res = avahi_browser_start(self,
			(const char * const *)self->services,
			self->services_count);
	if (res < 0) {
		ARSDK_LOG_ERRNO("avahi_browser_start", -res);
		arsdk_discovery_stop(self->parent);
	}

	return res;

#else /* !BUILD_AVAHI_CLIENT*/

	return 0;

#endif /* !BUILD_AVAHI_CLIENT*/
}

/**
 */
int arsdk_discovery_avahi_stop(struct arsdk_discovery_avahi *self)
{
	int res = 0;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	arsdk_discovery_stop(self->parent);

#ifdef BUILD_AVAHI_CLIENT
	res = avahi_browser_stop(self);
	if (res < 0)
		ARSDK_LOG_ERRNO("avahi_browser_stop", -res);
#endif /* BUILD_AVAHI_CLIENT*/
	return res;
}
