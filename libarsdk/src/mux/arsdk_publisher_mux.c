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
#include "arsdk_mux_log.h"

#ifdef BUILD_LIBMUX
#include "arsdk_mux.h"

/** */
struct arsdk_publisher_mux {
	struct arsdk_backend_mux         *backend;
	struct arsdk_device_mngr         *mngr;
	struct mux_ctx                   *mux;
	char                             *name;
	enum arsdk_device_type           type;
	char                             *id;
	int                              running;
};

/**
 */
POMP_ATTRIBUTE_FORMAT_PRINTF(3, 4)
static int publisher_mux_write_msg(struct arsdk_publisher_mux *self,
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

static void publisher_mux_rx_discover(struct arsdk_publisher_mux *self,
		struct pomp_msg *msg)
{
	if (!self->running)
		return;

	publisher_mux_write_msg(self, MUX_ARSDK_MSG_ID_DEVICE_ADDED,
			MUX_ARSDK_MSG_FMT_ENC_DEVICE_ADDED,
			self->name,
			self->type,
			self->id);
}

static void publisher_mux_rx_data(struct arsdk_publisher_mux *self,
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
	case MUX_ARSDK_MSG_ID_DISCOVER:
		publisher_mux_rx_discover(self, msg);
	break;

	default:
		ARSDK_LOGE("unsupported publisher mux msg %d",
			pomp_msg_get_id(msg));
	break;
	}

	pomp_msg_destroy(msg);
}

/**
 */
static void publisher_mux_channel_cb(struct mux_ctx *mux,
		uint32_t chanid,
		enum mux_channel_event event,
		struct pomp_buffer *buf,
		void *userdata)
{
	struct arsdk_publisher_mux *self = userdata;

	switch (event) {
	case MUX_CHANNEL_RESET:
		ARSDK_LOGI("publisher mux channel reset");
	break;
	case MUX_CHANNEL_DATA:
		publisher_mux_rx_data(self, chanid, buf, userdata);
	break;
	default:
		ARSDK_LOGE("unsupported publish channel event %d", event);
	break;
	}
}

#endif /* BUILD_LIBMUX */

/**
 */
int arsdk_publisher_mux_new(struct arsdk_backend_mux *backend,
		struct mux_ctx *mux,
		struct arsdk_publisher_mux **ret_obj)
{
#ifdef BUILD_LIBMUX
	struct arsdk_publisher_mux *self = NULL;
#endif
	int res = 0;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(backend != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(mux != NULL, -EINVAL);

#ifndef BUILD_LIBMUX
	res = -ENOSYS;
#else
	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Initialize parameters */
	self->backend = backend;
	self->mux = mux;
	mux_ref(self->mux);

	/* Open channel publish/discovery */
	res = mux_channel_open(self->mux, MUX_ARSDK_CHANNEL_ID_DISCOVERY,
			&publisher_mux_channel_cb, self);
	if (res < 0) {
		ARSDK_LOG_ERRNO("mux_channel_open", -res);
		goto error;
	}

	*ret_obj = self;
	res = 0;

	/* Cleanup in case of error */
error:
	if (res != 0)
		arsdk_publisher_mux_destroy(self);
#endif /* BUILD_LIBMUX */
	return res;
}

int arsdk_publisher_mux_destroy(struct arsdk_publisher_mux *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
#ifndef BUILD_LIBMUX
	return -ENOSYS;
#else
	mux_channel_close(self->mux, MUX_ARSDK_CHANNEL_ID_DISCOVERY);
	mux_unref(self->mux);
	free(self->name);
	free(self->id);
	free(self);
	return 0;
#endif
}

int arsdk_publisher_mux_start(struct arsdk_publisher_mux *self,
		const struct arsdk_publisher_cfg *cfg)
{
	int res;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg->name != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg->name[0] != '\0', -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg->id != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg->id[0] != '\0', -EINVAL);

#ifndef BUILD_LIBMUX
	res = -ENOSYS;
#else
	/* set running flag */
	self->running = 1;

	/* Send discover request */
	res = publisher_mux_write_msg(self, MUX_ARSDK_MSG_ID_DEVICE_ADDED,
			MUX_ARSDK_MSG_FMT_ENC_DEVICE_ADDED,
			cfg->name,
			cfg->type,
			cfg->id);
	if (res < 0) {
		self->running = 0;
		goto error;
	}

	/* Copy discovery config */
	self->name = cfg->name ? strdup(cfg->name) : NULL;
	self->id = cfg->id ? strdup(cfg->id) : NULL;
	self->type = cfg->type;
error:
#endif
	return res;
}

int arsdk_publisher_mux_stop(struct arsdk_publisher_mux *self)
{
#ifndef BUILD_LIBMUX
	return -ENOSYS;
#else
	int res;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	/* Notify remove */
	res = publisher_mux_write_msg(self, MUX_ARSDK_MSG_ID_DEVICE_REMOVED,
			MUX_ARSDK_MSG_FMT_ENC_DEVICE_REMOVED,
			self->name,
			self->type,
			self->id);

	if (res < 0 && res != -EPIPE)
		ARSDK_LOG_ERRNO("publisher_mux_write_msg", -res);

	/* clear running flag */
	self->running = 0;
	free(self->name);
	free(self->id);
	self->name = NULL;
	self->id = NULL;
	return 0;
#endif
}

