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
#include "arsdkctrl_mux_log.h"

#ifdef BUILD_LIBMUX

#include <mux/arsdk_mux.h>

/** */
struct arsdk_discovery_mux {
	struct arsdk_discovery      *parent;
	struct mux_ctx              *mux;
	enum arsdk_device_type      *types;
	size_t                      n_types;
};

static int is_devtype_supported(struct arsdk_discovery_mux *self,
		enum arsdk_device_type type)
{
	uint32_t i;
	for (i = 0; i < self->n_types; i++) {
		if (self->types[i] == type)
			return 1;
	}

	return 0;
}

/**
 */
POMP_ATTRIBUTE_FORMAT_PRINTF(3, 4)
static int discovery_mux_write_msg(struct arsdk_discovery_mux *self,
		uint32_t msgid, const char *fmt, ...)
{
	int res = 0;
	struct pomp_msg *msg = NULL;
	va_list args;

	msg = pomp_msg_new();
	if (msg == NULL)
		return -ENOMEM;

	va_start(args, fmt);
	res = pomp_msg_writev(msg, msgid, fmt, args);
	va_end(args);
	if (res < 0) {
		ARSDK_LOG_ERRNO("pomp_msg_write", -res);
		goto out;
	}

	res = mux_encode(self->mux, MUX_ARSDK_CHANNEL_ID_DISCOVERY,
			pomp_msg_get_buffer(msg));
	if (res < 0 && res != -EPIPE) {
		ARSDK_LOG_ERRNO("mux_encode", -res);
		goto out;
	}

out:
	pomp_msg_destroy(msg);
	return res;
}

static void discovery_mux_rx_device_added(struct arsdk_discovery_mux *self,
		struct pomp_msg *msg)
{
	int res = 0;
	char *name = NULL;
	uint32_t type = 0;
	char *id = NULL;
	struct arsdk_discovery_device_info info;

	res = pomp_msg_read(msg, MUX_ARSDK_MSG_FMT_DEC_DEVICE_ADDED,
			&name, &type, &id);
	if (res < 0) {
		ARSDK_LOG_ERRNO("pomp_msg_read", -res);
		return;
	}

	/* setup device info */
	memset(&info, 0, sizeof(info));
	info.name = name;
	info.type = type;
	info.id = id;
	info.api = mux_get_remote_version(self->mux) == MUX_PROTOCOL_VERSION ?
			ARSDK_DEVICE_API_FULL : ARSDK_DEVICE_API_UPDATE_ONLY;

	/* add device if type is supported */
	if (is_devtype_supported(self, info.type))
		arsdk_discovery_add_device(self->parent, &info);

	free(name);
	free(id);
}

static void discovery_mux_rx_device_removed(struct arsdk_discovery_mux *self,
		struct pomp_msg *msg)
{
	int res = 0;
	char *name = NULL;
	uint32_t type = 0;
	char *id = NULL;
	struct arsdk_discovery_device_info info;

	memset(&info, 0, sizeof(info));

	res = pomp_msg_read(msg, MUX_ARSDK_MSG_FMT_DEC_DEVICE_REMOVED,
			&name, &type, &id);
	if (res < 0) {
		ARSDK_LOG_ERRNO("pomp_msg_read", -res);
		return;
	}

	/* setup device info */
	memset(&info, 0, sizeof(info));
	info.name = name;
	info.type = type;
	info.id = id;
	info.api = mux_get_remote_version(self->mux) == MUX_PROTOCOL_VERSION ?
			ARSDK_DEVICE_API_FULL : ARSDK_DEVICE_API_UPDATE_ONLY;

	/* remove device if type is supported */
	if (is_devtype_supported(self, info.type))
		arsdk_discovery_remove_device(self->parent, &info);

	free(name);
	free(id);
}

static void discovery_mux_rx_data(struct arsdk_discovery_mux *self,
		uint32_t chanid,
		struct pomp_buffer *buf,
		void *userdata)
{
	struct pomp_msg *msg = NULL;

	/* Create pomp message from buffer */
	msg = pomp_msg_new_with_buffer(buf);
	if (msg == NULL)
		return;

	/* Decode message */
	switch (pomp_msg_get_id(msg)) {
	case MUX_ARSDK_MSG_ID_DEVICE_ADDED:
		discovery_mux_rx_device_added(self, msg);
	break;

	case MUX_ARSDK_MSG_ID_DEVICE_REMOVED:
		discovery_mux_rx_device_removed(self, msg);
	break;

	default:
		ARSDK_LOGE("unsupported discovery mux msg %d",
			pomp_msg_get_id(msg));
	break;
	}

	pomp_msg_destroy(msg);
}

static int discovery_mux_open_discovery_channel(
		struct arsdk_discovery_mux *self);

/**
 */
static void discovery_mux_channel_cb(struct mux_ctx *mux,
		uint32_t chanid,
		enum mux_channel_event event,
		struct pomp_buffer *buf,
		void *userdata)
{
	struct arsdk_discovery_mux *self = userdata;

	switch (event) {
	case MUX_CHANNEL_RESET:
		ARSDK_LOGI("discovery mux channel reset");
		/* stop discovery */
		arsdk_discovery_stop(self->parent);
		/* reopen discovery channel */
		discovery_mux_open_discovery_channel(self);
		/* start discovery */
		arsdk_discovery_start(self->parent);

	break;
	case MUX_CHANNEL_DATA:
		/* process received mux packet */
		discovery_mux_rx_data(self, chanid, buf, userdata);
	break;
	default:
		ARSDK_LOGE("unsupported discovery channel event %d", event);
	break;
	}
}


/**
 */
static
int discovery_mux_open_discovery_channel(struct arsdk_discovery_mux *self)
{
	int ret;
	ret = mux_channel_open(self->mux, MUX_ARSDK_CHANNEL_ID_DISCOVERY,
			&discovery_mux_channel_cb, self);
	if (ret < 0)
		ARSDK_LOG_ERRNO("mux_channel_open", -ret);

	return ret;
}



#endif /* BUILD_LIBMUX */

/**
 */
int arsdk_discovery_mux_new(struct arsdk_ctrl *ctrl,
		struct arsdkctrl_backend_mux *backend,
		const struct arsdk_discovery_cfg *cfg,
		struct mux_ctx *mux,
		struct arsdk_discovery_mux **ret_obj)
{
#ifdef BUILD_LIBMUX
	struct arsdk_discovery_mux *self = NULL;
#endif
	int res = 0;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(backend != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ctrl != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(mux != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg->types != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg->count > 0, -EINVAL);

#ifndef BUILD_LIBMUX
	res = -ENOSYS;
#else
	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Initialize parameters */
	self->mux = mux;
	mux_ref(self->mux);

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
	res = arsdk_discovery_new("mux",
			arsdkctrl_backend_mux_get_parent(backend), ctrl,
			&self->parent);
	if (res < 0) {
		ARSDK_LOG_ERRNO("arsdk_discovery_new", -res);
		goto error;
	}

	/* open channel for discovery */
	res = discovery_mux_open_discovery_channel(self);
	if (res < 0)
		goto error;

	*ret_obj = self;
	res = 0;

	/* Cleanup in case of error */
error:
	if (res != 0)
		arsdk_discovery_mux_destroy(self);
#endif /* BUILD_LIBMUX */
	return res;
}


int arsdk_discovery_mux_destroy(struct arsdk_discovery_mux *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
#ifndef BUILD_LIBMUX
	return -ENOSYS;
#else
	arsdk_discovery_destroy(self->parent);
	mux_channel_close(self->mux, MUX_ARSDK_CHANNEL_ID_DISCOVERY);
	mux_unref(self->mux);
	free(self->types);
	free(self);
	return 0;
#endif
}

int arsdk_discovery_mux_start(struct arsdk_discovery_mux *self)
{
	int res;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

#ifndef BUILD_LIBMUX
	res = -ENOSYS;
#else
	/* start discovery */
	res = arsdk_discovery_start(self->parent);
	if (res < 0) {
		ARSDK_LOG_ERRNO("arsdk_discovery_start", -res);
		return res;
	}

	/* Send a discover request */
	res = discovery_mux_write_msg(self, MUX_ARSDK_MSG_ID_DISCOVER, NULL);
	if (res < 0) {
		ARSDK_LOG_ERRNO("discovery_mux_write_msg", -res);
		arsdk_discovery_stop(self->parent);
	}
#endif
	return res;
}

int arsdk_discovery_mux_stop(struct arsdk_discovery_mux *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
#ifndef BUILD_LIBMUX
	return -ENOSYS;
#else
	return arsdk_discovery_stop(self->parent);
#endif
}
