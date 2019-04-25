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

#ifdef BUILD_LIBULOG
ULOG_DECLARE_TAG(arsdk_mux);
#endif /* BUILD_LIBULOG */

#ifdef BUILD_LIBMUX

#include "arsdk_mux.h"

/** */
enum peer_conn_state {
	PEER_CONN_STATE_PENDING,
	PEER_CONN_STATE_CONNECTED,
};

/** */
struct arsdk_peer_conn {
	struct arsdk_peer                      *peer;
	struct arsdk_backend_mux               *backend;
	struct arsdk_peer_conn_internal_cbs    cbs;
	enum peer_conn_state                   state;
	struct mux_ctx                         *mux;
	struct pomp_loop                       *loop;
	struct arsdk_transport_mux             *transport;
};

/** */
struct arsdk_backend_mux {
	struct arsdk_backend                   *parent;
	struct mux_ctx                         *mux;
	int                                    stream_supported;

	struct {
		struct arsdk_backend_listen_cbs  cbs;
		struct arsdk_peer_conn           *conn;
		int                              running;
	} listen;
};

/**
 */
static int peer_conn_setup_transport(struct arsdk_peer_conn *self)
{
	int res = 0;
	struct arsdk_transport_mux_cfg cfg;

	memset(&cfg, 0, sizeof(cfg));
	cfg.stream_supported = self->backend->stream_supported;
	res = arsdk_transport_mux_new(self->mux, self->loop, &cfg,
			&self->transport);
	return res;
}

/**
 */
static int peer_conn_destroy(struct arsdk_peer_conn *self)
{
	int res = 0;

	/* Cancel peer */
	if (self->peer != NULL) {
		/* cancel peer if needed */
		arsdk_peer_cancel(self->peer, self);
		/* destroy peer */
		arsdk_backend_destroy_peer(self->backend->parent, self->peer);
		self->peer = NULL;
	}

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

	/* Free other resources */
	free(self);
	return 0;
}

/**
 */
static int peer_conn_new(struct arsdk_backend_mux *backend,
		struct arsdk_peer_conn **ret_conn)
{
	struct arsdk_peer_conn *self = NULL;

	if (ret_conn == NULL)
		return -EINVAL;
	*ret_conn = NULL;

	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	self->backend = backend;
	self->mux = backend->mux;
	self->state = PEER_CONN_STATE_PENDING;
	*ret_conn = self;
	return 0;
}

/**
 */
POMP_ATTRIBUTE_FORMAT_PRINTF(3, 4)
static int backend_mux_write_msg(struct arsdk_backend_mux *self,
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
static void backend_mux_rx_conn_req(struct arsdk_backend_mux *self,
		struct pomp_msg *msg)
{
	int res = 0;
	char *ctrl_name = NULL;
	char *ctrl_type = NULL;
	char *device_id = NULL;
	char *rxjson = NULL;
	struct arsdk_peer_info info;
	const struct arsdk_peer_info *pinfo = NULL;

	memset(&info, 0, sizeof(info));

	res = pomp_msg_read(msg, MUX_ARSDK_MSG_FMT_DEC_CONN_REQ,
			&ctrl_name,
			&ctrl_type,
			&device_id,
			&rxjson);
	if (res < 0) {
		ARSDK_LOG_ERRNO("pomp_msg_read", -res);
		return;
	}

	/* ignore connection request if listen not started */
	if (!self->listen.running) {
		ARSDK_LOGI("ignoring connection request: listen not started");
		goto out;
	}

	/* Only one pending connection request at a time */
	if (self->listen.conn != NULL || peer_conn_new(
			self, &self->listen.conn) < 0) {
		ARSDK_LOGI("Connection request already in progress");
		goto out;
	}

	/* Create peer */
	info.ctrl_name = ctrl_name;
	info.ctrl_type = ctrl_type;
	info.device_id = device_id;
	info.json = rxjson;

	/* create peer */
	res = arsdk_backend_create_peer(self->parent, &info,
			self->listen.conn, &self->listen.conn->peer);
	if (res < 0)
		goto out;

	res = arsdk_peer_get_info(self->listen.conn->peer, &pinfo);
	if (res < 0)
		goto out;

	/* Notify connection request */
	(*self->listen.cbs.conn_req)(self->listen.conn->peer,
			pinfo, self->listen.cbs.userdata);

out:
	free(ctrl_name);
	free(ctrl_type);
	free(device_id);
	free(rxjson);
}

static void backend_mux_rx_data(struct arsdk_backend_mux *backend,
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
	case MUX_ARSDK_MSG_ID_CONN_REQ:
		backend_mux_rx_conn_req(backend, msg);
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
	struct arsdk_backend_mux *self = userdata;
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
static int arsdk_backend_mux_accept_peer_conn(
		struct arsdk_backend *base,
		struct arsdk_peer *peer,
		struct arsdk_peer_conn *conn,
		const struct arsdk_peer_conn_cfg *cfg,
		const struct arsdk_peer_conn_internal_cbs *cbs,
		struct pomp_loop *loop)
{
	int res = 0;
	struct arsdk_backend_mux *self = arsdk_backend_get_child(base);

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(peer != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(conn != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->connected != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->disconnected != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->canceled != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(loop != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(conn->peer == peer, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(self->listen.conn == conn, -EINVAL);

	/* Save information */
	conn->cbs = *cbs;
	conn->loop = loop;

	/* Setup transport */
	res = peer_conn_setup_transport(conn);
	if (res < 0)
		goto error;

	res = backend_mux_write_msg(self, MUX_ARSDK_MSG_ID_CONN_RESP,
			MUX_ARSDK_MSG_FMT_ENC_CONN_RESP,
			0, cfg->json ? cfg->json : "{}");
	if (res < 0)
		goto error;

	/* We don't need the connection anymore */
	self->listen.conn = NULL;

	/* Notify connection */
	conn->state = PEER_CONN_STATE_CONNECTED;
	(*conn->cbs.connected)(peer, conn,
			arsdk_transport_mux_get_parent(conn->transport),
			conn->cbs.userdata);

	/* Success */
	return 0;

	/* TODO: Cleanup in case of error */
error:
	return res;
}

/**
 */
static int arsdk_backend_mux_reject_peer_conn(struct arsdk_backend *base,
		struct arsdk_peer *peer,
		struct arsdk_peer_conn *conn)
{
	struct arsdk_backend_mux *self = arsdk_backend_get_child(base);

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(peer != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(conn != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(conn->peer == peer, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(self->listen.conn == conn, -EINVAL);

	backend_mux_write_msg(self, MUX_ARSDK_MSG_ID_CONN_RESP,
			MUX_ARSDK_MSG_FMT_ENC_CONN_RESP,
			-1, "");

	/* Cleanup connection */
	peer_conn_destroy(conn);
	self->listen.conn = NULL;
	return 0;
}

/**
 */
static int arsdk_backend_mux_stop_peer_conn(struct arsdk_backend *base,
		struct arsdk_peer *peer,
		struct arsdk_peer_conn *conn)
{
	struct arsdk_backend_mux *self = arsdk_backend_get_child(base);

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(peer != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(conn != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(conn->peer == peer, -EINVAL);

	/* If this is the pending peer, it is actually a reject */
	if (self->listen.conn == conn) {
		ARSDK_LOGW("peer %p: reject instead of disconnect", peer);
		return arsdk_backend_mux_reject_peer_conn(base, peer, conn);
	}

	/* Notify disconnection/cancellation */
	if (conn->state == PEER_CONN_STATE_CONNECTED) {
		(*conn->cbs.disconnected)(peer, conn, conn->cbs.userdata);
	} else {
		(*conn->cbs.canceled)(peer, conn,
				ARSDK_CONN_CANCEL_REASON_LOCAL,
				conn->cbs.userdata);
	}

	/* Cleanup connection */
	peer_conn_destroy(conn);
	return 0;
}

int arsdk_backend_mux_start_listen(struct arsdk_backend_mux *self,
		const struct arsdk_backend_listen_cbs *cbs)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->conn_req != NULL, -EINVAL);

	if (self->listen.running)
		return 0;

	self->listen.cbs = *cbs;
	self->listen.running = 1;
	return 0;
}

int arsdk_backend_mux_stop_listen(struct arsdk_backend_mux *self)
{
	if (!self->listen.running)
		return -EPERM;

	memset(&self->listen.cbs, 0, sizeof(self->listen.cbs));
	self->listen.running = 0;
	return 0;
}

/** */
static const struct arsdk_backend_ops s_arsdk_backend_mux_ops = {
	.accept_peer_conn = &arsdk_backend_mux_accept_peer_conn,
	.reject_peer_conn = &arsdk_backend_mux_reject_peer_conn,
	.stop_peer_conn = &arsdk_backend_mux_stop_peer_conn,
};

/**
 */
int arsdk_backend_mux_new(struct arsdk_mngr *mngr,
		const struct arsdk_backend_mux_cfg *cfg,
		struct arsdk_backend_mux **ret_obj)
{
	int res = 0;
	struct arsdk_backend_mux *self = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(cfg != NULL, -EINVAL);

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Setup base structure */
	res = arsdk_backend_new(self, mngr, "mux", ARSDK_BACKEND_TYPE_MUX,
			&s_arsdk_backend_mux_ops, &self->parent);
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
int arsdk_backend_mux_destroy(struct arsdk_backend_mux *self)
{
	int res;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	/* destroy backend */
	arsdk_backend_destroy(self->parent);

	/* Close mux channel for backend */
	res = mux_channel_close(self->mux, MUX_ARSDK_CHANNEL_ID_BACKEND);
	if (res < 0)
		ARSDK_LOG_ERRNO("mux_channel_close", -res);

	/* Free resources */
	mux_unref(self->mux);
	free(self);
	return 0;
}

struct arsdk_backend *
arsdk_backend_mux_get_parent(struct arsdk_backend_mux *self)
{
	return self ? self->parent : NULL;
}

struct mux_ctx *arsdk_backend_mux_get_mux_ctx(
		struct arsdk_backend_mux *self)
{
	return self == NULL ? NULL : self->mux;
}

#else /* !BUILD_LIBMUX */

/**
 */
int arsdk_backend_mux_new(struct arsdk_mngr *mngr,
		const struct arsdk_backend_mux_cfg *cfg,
		struct arsdk_backend_mux **ret_obj)
{
	return -ENOSYS;
}

/**
 */
int arsdk_backend_mux_destroy(struct arsdk_backend_mux *self)
{
	return -ENOSYS;
}

/**
 */
struct mux_ctx *arsdk_backend_mux_get_mux_ctx(struct arsdk_backend_mux *self)
{
	return NULL;
}

/**
 */
int arsdk_backend_mux_start_listen(struct arsdk_backend_mux *self,
		const struct arsdk_backend_listen_cbs *cbs)
{
	return -ENOSYS;
}

/**
 */
int arsdk_backend_mux_stop_listen(struct arsdk_backend_mux *self)
{
	return -ENOSYS;
}

struct arsdk_backend *
arsdk_backend_mux_get_parent(struct arsdk_backend_mux *self)
{
	return NULL;
}

#endif /* !BUILD_LIBMUX */
