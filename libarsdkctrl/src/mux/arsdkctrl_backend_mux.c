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

#ifdef BUILD_LIBULOG
ULOG_DECLARE_TAG(arsdkctrl_mux);
#endif /* BUILD_LIBULOG */

#ifdef BUILD_LIBMUX

#include <mux/arsdk_mux.h>

/** */
enum device_conn_state {
	DEVICE_CONN_STATE_IDLE,
	DEVICE_CONN_STATE_PENDING,
	DEVICE_CONN_STATE_CONNECTED,
};

/** */
struct arsdk_device_conn {
	struct arsdk_device                   *device;
	struct arsdk_device_conn_internal_cbs  cbs;
	enum device_conn_state                 state;
	struct mux_ctx                         *mux;
	struct pomp_loop                       *loop;
	struct arsdk_transport_mux             *transport;
	char                                   *ctrl_name;
	char                                   *ctrl_type;
	char                                   *device_id;
	char                                   *txjson;
	int                                    stream_supported;
};

/** */
struct arsdkctrl_backend_mux {
	struct arsdkctrl_backend               *parent;
	struct mux_ctx                         *mux;
	int                                    stream_supported;

	struct {
		struct arsdk_device_conn       *conn;
	} client;
};

/**
 */
static int device_conn_setup_transport(struct arsdk_device_conn *self)
{
	int res = 0;
	struct arsdk_transport_mux_cfg cfg;

	memset(&cfg, 0, sizeof(cfg));
	cfg.stream_supported = self->stream_supported;
	res = arsdk_transport_mux_new(self->mux, self->loop, &cfg,
				&self->transport);
	return res;
}

/**
 */
static void device_conn_destroy(struct arsdk_device_conn *self)
{
	int res = 0;

	/* Stop and destroy transport */
	if (self->transport != NULL) {
		res = arsdk_transport_stop(arsdk_transport_mux_get_parent(
				self->transport));
		if (res < 0)
			ARSDK_LOG_ERRNO("arsdk_transport_stop", -res);
		res = arsdk_transport_destroy(arsdk_transport_mux_get_parent(
				self->transport));
		if (res < 0)
			ARSDK_LOG_ERRNO("arsdk_transport_destroy", -res);
	}

	free(self->ctrl_name);
	free(self->ctrl_type);
	free(self->txjson);
	free(self);
}

/**
 */
static int device_conn_new(
		struct arsdk_device *device,
		const struct arsdk_device_conn_cfg *cfg,
		const struct arsdk_device_conn_internal_cbs *cbs,
		struct mux_ctx *mux,
		int stream_supported,
		struct pomp_loop *loop,
		struct arsdk_device_conn **ret_conn)
{
	struct arsdk_device_conn *self = NULL;

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Save device */
	self->device = device;

	/* Copy connection config, initialize state */
	self->mux = mux;
	self->loop = loop;
	self->ctrl_name = xstrdup(cfg->ctrl_name);
	self->ctrl_type = xstrdup(cfg->ctrl_type);
	self->txjson = xstrdup(cfg->json);
	self->cbs = *cbs;
	self->state = DEVICE_CONN_STATE_IDLE;
	self->stream_supported = stream_supported;

	*ret_conn = self;
	return 0;
};

/**
 */
POMP_ATTRIBUTE_FORMAT_PRINTF(3, 4)
static int backend_mux_write_msg(struct arsdkctrl_backend_mux *self,
		uint32_t msgid,
		const char *fmt, ...)
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

	res = mux_encode(self->mux, MUX_ARSDK_CHANNEL_ID_BACKEND,
			pomp_msg_get_buffer(msg));
	if (res < 0 && res != -EPIPE) {
		ARSDK_LOG_ERRNO("mux_encode", -res);
		goto out;
	}

out:
	pomp_msg_destroy(msg);
	return res;
}

/**
 */
static void backend_mux_rx_conn_resp(struct arsdkctrl_backend_mux *self,
		struct pomp_msg *msg)
{
	int res = 0;
	struct arsdk_device_conn *conn = self->client.conn;
	int32_t status = 0;
	char *rxjson = NULL;
	const struct arsdk_device_info *info = NULL;
	struct arsdk_device_info newinfo;

	if (conn == NULL) {
		ARSDK_LOGW("No connection pending");
		return;
	}

	res = pomp_msg_read(msg, MUX_ARSDK_MSG_FMT_DEC_CONN_RESP,
			&status,
			&rxjson);
	if (res < 0) {
		ARSDK_LOG_ERRNO("pomp_msg_read", -res);
		return;
	}

	if (status != 0) {
		ARSDK_LOGI("Connection refused");

		/* Notify rejection */
		self->client.conn = NULL;
		(*conn->cbs.canceled)(conn->device, conn,
				ARSDK_CONN_CANCEL_REASON_REJECTED,
				conn->cbs.userdata);
		device_conn_destroy(conn);
		goto out;
	}

	/* Update info */
	res = arsdk_device_get_info(conn->device, &info);
	if (res < 0)
		goto out;
	newinfo = *info;
	newinfo.json = rxjson;

	/* Notify connection */
	self->client.conn = NULL;
	conn->state = DEVICE_CONN_STATE_CONNECTED;
	(*conn->cbs.connected)(conn->device, &newinfo, conn,
			arsdk_transport_mux_get_parent(conn->transport),
			conn->cbs.userdata);

out:
	free(rxjson);
}

static void backend_mux_rx_data(struct arsdkctrl_backend_mux *backend,
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
	/*case MUX_ARSDK_MSG_ID_CONN_REQ:
		backend_mux_rx_conn_req(backend, msg);
	break;*/

	case MUX_ARSDK_MSG_ID_CONN_RESP:
		backend_mux_rx_conn_resp(backend, msg);
	break;
	default:
		ARSDK_LOGE("unsupported backend mux msg %d",
			pomp_msg_get_id(msg));
	break;
	}

	pomp_msg_destroy(msg);
}

/**
 */
static void backend_mux_channel_cb(struct mux_ctx *mux,
		uint32_t chanid,
		enum mux_channel_event event,
		struct pomp_buffer *buf,
		void *userdata)
{
	struct arsdkctrl_backend_mux *self = userdata;
	int res;

	switch (event) {
	case MUX_CHANNEL_RESET:
		ARSDK_LOGI("backend mux channel reset");
		/* Open channels for backend */
		res = mux_channel_open(self->mux, MUX_ARSDK_CHANNEL_ID_BACKEND,
				&backend_mux_channel_cb, self);
		if (res < 0)
			ARSDK_LOG_ERRNO("mux_channel_open", -res);
	break;
	case MUX_CHANNEL_DATA:
		backend_mux_rx_data(self, chanid, buf, userdata);
	break;
	default:
		ARSDK_LOGE("unsupported backend channel event %d", event);
	break;
	}
}

/**
 */
static int arsdkctrl_backend_mux_start_device_conn(
		struct arsdkctrl_backend *base,
		struct arsdk_device *device,
		struct arsdk_device_info *info,
		const struct arsdk_device_conn_cfg *cfg,
		const struct arsdk_device_conn_internal_cbs *cbs,
		struct pomp_loop *loop,
		struct arsdk_device_conn **ret_conn)
{
	int res = 0;
	struct arsdkctrl_backend_mux *self =
			arsdkctrl_backend_get_child(base);
	struct arsdk_device_conn *conn = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_conn != NULL, -EINVAL);
	*ret_conn = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(device != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(info != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->connecting != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->connected != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->disconnected != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->canceled != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(loop != NULL, -EINVAL);

	if (self->client.conn != NULL)
		return -EBUSY;

	/* Create device connection context */
	res = device_conn_new(device, cfg, cbs, self->mux,
			self->stream_supported, loop, &conn);
	if (res < 0)
		goto error;

	/* Setup transport */
	res = device_conn_setup_transport(conn);
	if (res < 0)
		goto error;

	res = backend_mux_write_msg(self, MUX_ARSDK_MSG_ID_CONN_REQ,
			MUX_ARSDK_MSG_FMT_ENC_CONN_REQ,
			conn->ctrl_name == NULL ? "" : conn->ctrl_name,
			conn->ctrl_type == NULL ? "" : conn->ctrl_type,
			conn->device_id == NULL ? "" : conn->device_id,
			conn->txjson == NULL ? "{}" : conn->txjson);
	if (res < 0)
		goto error;

	/* Success */
	self->client.conn = conn;
	conn->state = DEVICE_CONN_STATE_PENDING;
	*ret_conn = conn;
	(*conn->cbs.connecting)(device, conn, conn->cbs.userdata);
	return 0;

	/* Cleanup in case of error */
error:
	if (conn != NULL)
		device_conn_destroy(conn);
	return res;
}

/**
 */
static int arsdkctrl_backend_mux_stop_device_conn(
		struct arsdkctrl_backend *base,
		struct arsdk_device *device,
		struct arsdk_device_conn *conn)
{
	struct arsdkctrl_backend_mux *self =
			arsdkctrl_backend_get_child(base);

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(device != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(conn != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(conn->device == device, -EINVAL);

	/* Notify disconnection/cancellation */
	if (conn->state == DEVICE_CONN_STATE_CONNECTED) {
		(*conn->cbs.disconnected)(device, conn, conn->cbs.userdata);
	} else {
		(*conn->cbs.canceled)(device, conn,
				ARSDK_CONN_CANCEL_REASON_LOCAL,
				conn->cbs.userdata);
	}
	device_conn_destroy(conn);
	if (self->client.conn == conn)
		self->client.conn = NULL;
	return 0;
}

/** */
static const struct arsdkctrl_backend_ops s_arsdkctrl_backend_mux_ops = {
	.start_device_conn = &arsdkctrl_backend_mux_start_device_conn,
	.stop_device_conn = &arsdkctrl_backend_mux_stop_device_conn,
};

/**
 */
int arsdkctrl_backend_mux_new(struct arsdk_ctrl *ctrl,
		const struct arsdkctrl_backend_mux_cfg *cfg,
		struct arsdkctrl_backend_mux **ret_obj)
{
	int res = 0;
	struct arsdkctrl_backend_mux *self = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(cfg != NULL, -EINVAL);

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Setup base structure */
	res = arsdkctrl_backend_new(self, ctrl, "mux", ARSDK_BACKEND_TYPE_MUX,
			&s_arsdkctrl_backend_mux_ops, &self->parent);
	if (res < 0)
		goto error;

	/* Initialize structure */
	self->mux = cfg->mux;
	mux_ref(self->mux);
	self->stream_supported = cfg->stream_supported;

	/* Open channels for backend */
	res = mux_channel_open(self->mux, MUX_ARSDK_CHANNEL_ID_BACKEND,
			&backend_mux_channel_cb, self);
	if (res < 0) {
		ARSDK_LOG_ERRNO("mux_channel_open", -res);
		goto error;
	}

	/* Success */
	*ret_obj = self;
	return 0;

	/* TODO: Cleanup in case of error */
error:
	return res;
}


/**
 */
int arsdkctrl_backend_mux_destroy(struct arsdkctrl_backend_mux *self)
{
	int res;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	/* destroy backend */
	arsdkctrl_backend_destroy(self->parent);

	/* Close mux channel for backend */
	res = mux_channel_close(self->mux, MUX_ARSDK_CHANNEL_ID_BACKEND);
	if (res < 0)
		ARSDK_LOG_ERRNO("mux_channel_close", -res);

	/* Free resources */
	mux_unref(self->mux);
	free(self);
	return 0;
}

struct arsdkctrl_backend *
arsdkctrl_backend_mux_get_parent(struct arsdkctrl_backend_mux *self)
{
	return self ? self->parent : NULL;
}

struct mux_ctx *arsdkctrl_backend_mux_get_mux_ctx(
		struct arsdkctrl_backend_mux *self)
{
	return self == NULL ? NULL : self->mux;
}

#else /* !BUILD_LIBMUX */

/**
 */
int arsdkctrl_backend_mux_new(struct arsdk_ctrl *ctrl,
		const struct arsdkctrl_backend_mux_cfg *cfg,
		struct arsdkctrl_backend_mux **ret_obj)
{
	return -ENOSYS;
}

/**
 */
int arsdkctrl_backend_mux_destroy(struct arsdkctrl_backend_mux *self)
{
	return -ENOSYS;
}

/**
 */
struct mux_ctx *arsdkctrl_backend_mux_get_mux_ctx(
		struct arsdkctrl_backend_mux *self)
{
	return NULL;
}

struct arsdkctrl_backend *
arsdkctrl_backend_mux_get_parent(struct arsdkctrl_backend_mux *self)
{
	return NULL;
}

#endif /* !BUILD_LIBMUX */
