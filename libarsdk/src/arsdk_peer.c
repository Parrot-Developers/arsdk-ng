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
#include "cmd_itf/arsdk_cmd_itf_priv.h"
#include "arsdk_default_log.h"

/**
 */
static int cmd_itf_dispose(struct arsdk_cmd_itf *itf, void *userdata)
{
	struct arsdk_peer *self = userdata;
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(itf == self->cmd_itf, -EINVAL);
	self->cmd_itf = NULL;
	return 0;
}

/**
 */
static void recv_data(struct arsdk_transport *transport,
		const struct arsdk_transport_header *header,
		const struct arsdk_transport_payload *payload,
		void *userdata)
{
	struct arsdk_peer *self = userdata;

	if (self->cmd_itf == NULL ||
	    header->id < ARSDK_TRANSPORT_ID_CMD_MIN) {
		ARSDK_LOGW("Frame lost id=%d seq=%d", header->id, header->seq);
		return;
	}

	int res = arsdk_cmd_itf_recv_data(self->cmd_itf, header, payload);
	if (res < 0)
		ARSDK_LOG_ERRNO("arsdk_cmd_itf_recv_data", -res);
}

/**
 */
static void link_status(struct arsdk_transport *transport,
		enum arsdk_link_status status,
		void *userdata)
{
	struct arsdk_peer *self = userdata;
	(*self->cbs.link_status)(self, &self->info, status, self->cbs.userdata);
}

/**
 */
static void log_cb(struct arsdk_transport *transport,
		enum arsdk_cmd_dir dir,
		const void *header,
		size_t headerlen,
		const void *payload,
		size_t payloadlen,
		void *userdata)
{
	struct arsdk_peer *self = userdata;

	if (!self->cmd_itf || !self->cmd_itf->cbs.transport_log)
		return;

	(*self->cmd_itf->cbs.transport_log)(self->cmd_itf, dir,
			header, headerlen,
			payload, payloadlen,
			self->cmd_itf->cbs.userdata);
}

/**
 */
static void stop_interfaces(struct arsdk_peer *self)
{
	if (self->cmd_itf != NULL)
		arsdk_cmd_itf_stop(self->cmd_itf);
}

/**
 */
static void cleanup_connection(struct arsdk_peer *self)
{
	/* Clear interfaces */
	if (self->cmd_itf != NULL) {
		arsdk_cmd_itf_destroy(self->cmd_itf);
		self->cmd_itf = NULL;
	}

	/* Clear connection and transport */
	memset(&self->cbs, 0, sizeof(self->cbs));
	self->conn = NULL;
	self->transport = NULL;
}

/**
 */
static void connected(struct arsdk_peer *peer,
		struct arsdk_peer_conn *conn,
		struct arsdk_transport *transport,
		void *userdata)
{
	int res = 0;
	struct arsdk_transport_cbs cbs;

	/* Save transport and start it */
	peer->transport = transport;
	memset(&cbs, 0, sizeof(cbs));
	cbs.userdata = peer;
	cbs.recv_data = &recv_data;
	cbs.link_status = &link_status;
	cbs.log_cb = &log_cb;

	res = arsdk_transport_start(transport, &cbs);
	if (res < 0)
		ARSDK_LOG_ERRNO("arsdk_transport_start", -res);

	(*peer->cbs.connected)(peer, &peer->info, peer->cbs.userdata);
}

/**
 */
static void disconnected(struct arsdk_peer *peer,
		struct arsdk_peer_conn *conn,
		void *userdata)
{
	stop_interfaces(peer);
	(*peer->cbs.disconnected)(peer, &peer->info, peer->cbs.userdata);
	cleanup_connection(peer);
}

/**
 */
static void canceled(struct arsdk_peer *peer,
		struct arsdk_peer_conn *conn,
		enum arsdk_conn_cancel_reason reason,
		void *userdata)
{
	stop_interfaces(peer);
	(*peer->cbs.canceled)(peer, &peer->info, reason, peer->cbs.userdata);
	cleanup_connection(peer);
}

/**
 */
void arsdk_peer_destroy(struct arsdk_peer *self)
{
	if (self->conn != NULL)
		ARSDK_LOGW("peer %p still connected during destroy", self);

	free(self->ctrl_name);
	free(self->ctrl_type);
	free(self->ctrl_addr);
	free(self->device_id);
	free(self->json);
	free(self);
}

/**
 */
int arsdk_peer_new(struct arsdk_backend *backend,
		const struct arsdk_peer_info *info,
		uint16_t handle,
		struct arsdk_peer_conn *conn,
		struct arsdk_peer **ret_obj)
{
	struct arsdk_peer *self = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(backend != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(info != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(conn != NULL, -EINVAL);

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Initialize structure */
	self->backend = backend;
	self->conn = conn;
	self->handle = handle;

	/* Copy device info */
	self->ctrl_name = xstrdup(info->ctrl_name);
	self->ctrl_type = xstrdup(info->ctrl_type);
	self->ctrl_addr = xstrdup(info->ctrl_addr);
	self->device_id = xstrdup(info->device_id);
	self->json = xstrdup(info->json);

	self->info.backend_type = arsdk_backend_get_type(self->backend);
	self->info.ctrl_name = self->ctrl_name;
	self->info.ctrl_type = self->ctrl_type;
	self->info.ctrl_addr = self->ctrl_addr;
	self->info.device_id = self->device_id;
	self->info.json = self->json;

	*ret_obj = self;
	return 0;
}

/**
 */
uint16_t arsdk_peer_get_handle(struct arsdk_peer *self)
{
	return self ? self->handle : ARSDK_INVALID_HANDLE;
}

/**
 */
int arsdk_peer_set_osdata(struct arsdk_peer *self, void *osdata)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	self->osdata = osdata;
	return 0;
}

/**
 */
void *arsdk_peer_get_osdata(struct arsdk_peer *self)
{
	return self == NULL ? NULL : self->osdata;
}

/**
 */
int arsdk_peer_get_info(struct arsdk_peer *self,
		const struct arsdk_peer_info **info)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(info != NULL, -EINVAL);
	*info = &self->info;
	return 0;
}

/**
 */
struct arsdk_backend *arsdk_peer_get_backend(struct arsdk_peer *self)
{
	return self == NULL ? NULL : self->backend;
}

/**
 */
int arsdk_peer_cancel(
		struct arsdk_peer *self,
		struct arsdk_peer_conn *conn)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(conn != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(self->conn == NULL
			|| self->conn == conn, -EINVAL);
	stop_interfaces(self);
	cleanup_connection(self);
	return 0;
}

/**
 */
int arsdk_peer_accept(
		struct arsdk_peer *self,
		const struct arsdk_peer_conn_cfg *cfg,
		const struct arsdk_peer_conn_cbs *cbs,
		struct pomp_loop *loop)
{
	struct arsdk_peer_conn_internal_cbs internal_cbs;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->connected != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->disconnected != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->canceled != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->link_status != NULL, -EINVAL);

	/* Must still be attached to a backend */
	if (self->backend == NULL)
		return -EPERM;

	/* Check that we still have a connection in progress */
	if (self->conn == NULL)
		return -EPERM;

	/* Save callbacks */
	self->cbs = *cbs;

	/* Setup internal callbacks */
	memset(&internal_cbs, 0, sizeof(internal_cbs));
	internal_cbs.userdata = self;
	internal_cbs.connected = &connected;
	internal_cbs.disconnected = &disconnected;
	internal_cbs.canceled = &canceled;

	/* Forward to backend, internal callback will be notified afterwards */
	return arsdk_backend_accept_peer_conn(self->backend, self,
			self->conn, cfg, &internal_cbs, loop);
}

/**
 */
int arsdk_peer_reject(struct arsdk_peer *self)
{
	int res = 0;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	/* Must still be attached to a backend */
	if (self->backend == NULL)
		return -EPERM;

	/* Nothing to do if no more connection in progress */
	if (self->conn == NULL)
		return 0;

	/* Forward to backend, (no callback called, but arsdk_peer_cancel will
	 * cleanup internal stuff) */
	res = arsdk_backend_reject_peer_conn(self->backend, self, self->conn);
	return res;
}

/**
 */
int arsdk_peer_disconnect(struct arsdk_peer *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	/* Must still be attached to a backend */
	if (self->backend == NULL)
		return -EPERM;

	/* Nothing to do if no more connection in progress */
	if (self->conn == NULL)
		return 0;

	/* Forward to backend, internal callback will be notified afterwards */
	return arsdk_backend_stop_peer_conn(self->backend, self, self->conn);
}

/** TODO: ask a central database to get this */
static const struct arsdk_cmd_queue_info s_tx_info_table[] = {
	{
		.type = ARSDK_TRANSPORT_DATA_TYPE_WITHACK,
		.id = ARSDK_TRANSPORT_ID_D2C_CMD_WITHACK,
		.max_tx_rate_ms = 0,
		.ack_timeout_ms = 150,
		.default_max_retry_count = -1,
		.overwrite = 0,
	},
	{
		.type = ARSDK_TRANSPORT_DATA_TYPE_NOACK,
		.id = ARSDK_TRANSPORT_ID_D2C_CMD_NOACK,
		.max_tx_rate_ms = 5,
		.ack_timeout_ms = -1,
		.default_max_retry_count = -1,
		.overwrite = 1,
	},
	{
		.type = ARSDK_TRANSPORT_DATA_TYPE_WITHACK,
		.id = ARSDK_TRANSPORT_ID_D2C_CMD_LOWPRIO,
		.max_tx_rate_ms = 0,
		.ack_timeout_ms = 150,
		.default_max_retry_count = -1,
		.overwrite = 0,
	},
};

/** */
static const uint32_t s_tx_count =
		sizeof(s_tx_info_table) / sizeof(s_tx_info_table[0]);

/**
 */
int arsdk_peer_create_cmd_itf(struct arsdk_peer *self,
		const struct arsdk_cmd_itf_cbs *cbs,
		struct arsdk_cmd_itf **ret_itf)
{
	int res = 0;
	struct arsdk_cmd_itf_internal_cbs internal_cbs;

	ARSDK_RETURN_ERR_IF_FAILED(ret_itf != NULL, -EINVAL);
	*ret_itf = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);

	if (self->backend == NULL)
		return -EPERM;
	if (self->transport == NULL)
		return -EPERM;
	if (self->cmd_itf != NULL)
		return -EBUSY;

	/* Create command interface */
	memset(&internal_cbs, 0, sizeof(internal_cbs));
	internal_cbs.userdata = self;
	internal_cbs.dispose = &cmd_itf_dispose;
	res = arsdk_cmd_itf_new(self->transport, cbs, &internal_cbs,
			&s_tx_info_table[0], s_tx_count,
			ARSDK_TRANSPORT_ID_ACKOFF, ret_itf);
	if (res == 0) {
		/* Keep it */
		self->cmd_itf = *ret_itf;
	}

	return res;
}

/**
 */
struct arsdk_cmd_itf *arsdk_peer_get_cmd_itf(struct arsdk_peer *self)
{
	return self ? self->cmd_itf : NULL;
}
