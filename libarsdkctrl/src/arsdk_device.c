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
#include "arsdk_cmd_itf_priv.h"
#include "arsdk_ftp_itf_priv.h"
#include "arsdk_media_itf_priv.h"
#include "arsdk_updater_itf_priv.h"
#include "arsdk_blackbox_itf_priv.h"
#include "arsdk_crashml_itf_priv.h"
#include "arsdk_flight_log_itf_priv.h"
#include "arsdk_pud_itf_priv.h"
#include "arsdkctrl_default_log.h"
#include "arsdk_ephemeris_itf_priv.h"
#include <libmux.h>

/**
 */
const char *arsdk_device_type_to_fld(enum arsdk_device_type dev_type)
{
	static const struct {
		enum arsdk_device_type type;
		const char *folder;
	} dev_flds[] = {
		{ARSDK_DEVICE_TYPE_BEBOP, "Bebop_Drone/"},
		{ARSDK_DEVICE_TYPE_BEBOP_2, "Bebop_2/"},
		{ARSDK_DEVICE_TYPE_PAROS, "Paros/"},
		{ARSDK_DEVICE_TYPE_ANAFI4K, "Anafi4k/"},
		{ARSDK_DEVICE_TYPE_ANAFI_THERMAL, "Anafi_Thermal/"},
		{ARSDK_DEVICE_TYPE_CHIMERA, "Chimera/"},
		{ARSDK_DEVICE_TYPE_SKYCTRL, "SkyController/"},
		{ARSDK_DEVICE_TYPE_SKYCTRL_2, "SkyController_2/"},
		{ARSDK_DEVICE_TYPE_SKYCTRL_2P, "SkyController_2/"},
		{ARSDK_DEVICE_TYPE_SKYCTRL_3, "SkyController_3/"},
		{ARSDK_DEVICE_TYPE_JS, "Jumping_Sumo/"},
		{ARSDK_DEVICE_TYPE_JS_EVO_LIGHT, "Jumping_Night/"},
		{ARSDK_DEVICE_TYPE_JS_EVO_RACE, "Jumping_Race/"},
		{ARSDK_DEVICE_TYPE_RS, "Rolling_Spider/"},
		{ARSDK_DEVICE_TYPE_RS_EVO_LIGHT, "Airborne_Nigh/"},
		{ARSDK_DEVICE_TYPE_RS_EVO_BRICK, "Airborne_Cargo/"},
		{ARSDK_DEVICE_TYPE_RS_EVO_HYDROFOIL, "Hydrofoil/"},
		{ARSDK_DEVICE_TYPE_POWERUP, "Power_Up/"},
		{ARSDK_DEVICE_TYPE_EVINRUDE, "Disco/"},
		{ARSDK_DEVICE_TYPE_WINGX, "Swing/"},
	};
	static const size_t dev_flds_count =
			sizeof(dev_flds) / sizeof(dev_flds[0]);
	size_t i = 0;

	for (i = 0; i < dev_flds_count; i++) {
		if (dev_type == dev_flds[i].type)
			return dev_flds[i].folder;
	}

	return NULL;
}

/**
 */
static int cmd_itf_dispose(struct arsdk_cmd_itf *itf, void *userdata)
{
	struct arsdk_device *self = userdata;
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(itf == self->cmd_itf, -EINVAL);
	self->cmd_itf = NULL;
	return 0;
}

/**
 */
static int ftp_itf_dispose(struct arsdk_ftp_itf *itf, void *userdata)
{
	struct arsdk_device *self = userdata;
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(itf == self->ftp_itf, -EINVAL);
	self->ftp_itf = NULL;
	return 0;
}

/**
 */
static void ftp_itf_socket_cb(struct arsdk_ftp_itf *itf, int fd,
		enum arsdk_socket_kind kind, void *userdata)
{
	struct arsdk_device *self = userdata;

	ARSDK_RETURN_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(self->backend != NULL, -EINVAL);

	arsdkctrl_backend_socket_cb(self->backend, fd, kind);
}

/**
 */
static void recv_data(struct arsdk_transport *transport,
		const struct arsdk_transport_header *header,
		const struct arsdk_transport_payload *payload,
		void *userdata)
{
	struct arsdk_device *self = userdata;

	switch (header->id) {
	case ARSDK_TRANSPORT_ID_D2C_CMD_NOACK:
	case ARSDK_TRANSPORT_ID_D2C_CMD_NOACK_BLE:
	case ARSDK_TRANSPORT_ID_D2C_CMD_WITHACK:
	case ARSDK_TRANSPORT_ID_D2C_CMD_WITHACK_BLE:
	case ARSDK_TRANSPORT_ID_D2C_CMD_ACK:
	case ARSDK_TRANSPORT_ID_D2C_CMD_ACK_BLE:
	case ARSDK_TRANSPORT_ID_D2C_CMD_HIGHPRIO_ACK:
	case ARSDK_TRANSPORT_ID_D2C_CMD_HIGHPRIO_ACK_BLE:
		if (self->cmd_itf != NULL)
			arsdk_cmd_itf_recv_data(self->cmd_itf, header, payload);
		break;

	default:
		ARSDK_LOGW("Frame lost id=%d", header->id);
		break;
	}
}

/**
 */
static void link_status(struct arsdk_transport *transport,
		enum arsdk_link_status status,
		void *userdata)
{
	struct arsdk_device *self = userdata;
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
	struct arsdk_device *self = userdata;

	if (!self->cmd_itf || !self->cmd_itf->cbs.transport_log)
		return;

	(*self->cmd_itf->cbs.transport_log)(self->cmd_itf, dir,
			header, headerlen,
			payload, payloadlen,
			self->cmd_itf->cbs.userdata);
}

/**
 */
static void stop_interfaces(struct arsdk_device *self)
{
	if (self->cmd_itf != NULL)
		arsdk_cmd_itf_stop(self->cmd_itf);
	if (self->ftp_itf != NULL)
		arsdk_ftp_itf_stop(self->ftp_itf);
	if (self->media_itf != NULL)
		arsdk_media_itf_stop(self->media_itf);
	if (self->updater_itf != NULL)
		arsdk_updater_itf_stop(self->updater_itf);
	if (self->blackbox_itf != NULL)
		arsdk_blackbox_itf_stop(self->blackbox_itf);
	if (self->crashml_itf != NULL)
		arsdk_crashml_itf_stop(self->crashml_itf);
	if (self->flight_log_itf != NULL)
		arsdk_flight_log_itf_stop(self->flight_log_itf);
	if (self->pud_itf != NULL)
		arsdk_pud_itf_stop(self->pud_itf);
	if (self->ephemeris_itf != NULL)
		arsdk_ephemeris_itf_stop(self->ephemeris_itf);
}

/**
 */
static void cleanup_connection(struct arsdk_device *self)
{
	/* Clear interfaces */
	if (self->cmd_itf != NULL) {
		arsdk_cmd_itf_destroy(self->cmd_itf);
		self->cmd_itf = NULL;
	}
	if (self->media_itf != NULL) {
		arsdk_media_itf_destroy(self->media_itf);
		self->media_itf = NULL;
	}
	if (self->updater_itf != NULL) {
		arsdk_updater_itf_destroy(self->updater_itf);
		self->updater_itf = NULL;
	}
	if (self->blackbox_itf != NULL) {
		arsdk_blackbox_itf_destroy(self->blackbox_itf);
		self->blackbox_itf = NULL;
	}
	if (self->crashml_itf != NULL) {
		arsdk_crashml_itf_destroy(self->crashml_itf);
		self->crashml_itf = NULL;
	}
	if (self->flight_log_itf != NULL) {
		arsdk_flight_log_itf_destroy(self->flight_log_itf);
		self->flight_log_itf = NULL;
	}
	if (self->pud_itf != NULL) {
		arsdk_pud_itf_destroy(self->pud_itf);
		self->pud_itf = NULL;
	}
	if (self->ephemeris_itf != NULL) {
		arsdk_ephemeris_itf_destroy(self->ephemeris_itf);
		self->ephemeris_itf = NULL;
	}
	if (self->ftp_itf != NULL) {
		arsdk_ftp_itf_destroy(self->ftp_itf);
		self->ftp_itf = NULL;
	}

	/* Clear connection and transport */
	memset(&self->cbs, 0, sizeof(self->cbs));
	self->conn = NULL;
	self->transport = NULL;
}

/**
 */
static void update_info(struct arsdk_device *self,
		const struct arsdk_device_info *info)
{
	/* Update internal strings */
	if (self->name != info->name) {
		free(self->name);
		self->name = xstrdup(info->name);
	}
	if (self->addr != info->addr) {
		free(self->addr);
		self->addr = xstrdup(info->addr);
	}
	if (self->id != info->id) {
		free(self->id);
		self->id = xstrdup(info->id);
	}
	if (self->json != info->json) {
		free(self->json);
		self->json = xstrdup(info->json);
	}

	/* Update info string pointers */
	self->info.name = self->name;
	self->info.addr = self->addr;
	self->info.id = self->id;
	self->info.json = self->json;

	/* Update non string fields */
	self->info.type = info->type;
	self->info.port = info->port;
}

/**
 */
static void connecting(struct arsdk_device *device,
		struct arsdk_device_conn *conn,
		void *userdata)
{
	device->info.state = ARSDK_DEVICE_STATE_CONNECTING;
	(*device->cbs.connecting)(device, &device->info, device->cbs.userdata);
}

/**
 */
static void connected(struct arsdk_device *device,
		const struct arsdk_device_info *info,
		struct arsdk_device_conn *conn,
		struct arsdk_transport *transport,
		void *userdata)
{
	int res = 0;
	struct arsdk_transport_cbs cbs;

	/* Save transport and start it */
	device->transport = transport;
	memset(&cbs, 0, sizeof(cbs));
	cbs.userdata = device;
	cbs.recv_data = &recv_data;
	cbs.link_status = &link_status;
	cbs.log_cb = &log_cb;

	res = arsdk_transport_start(transport, &cbs);
	if (res < 0)
		ARSDK_LOG_ERRNO("arsdk_transport_start", -res);

	if (info != NULL)
		update_info(device, info);
	device->info.state = ARSDK_DEVICE_STATE_CONNECTED;
	(*device->cbs.connected)(device, &device->info, device->cbs.userdata);
}

/**
 */
static void disconnected(struct arsdk_device *device,
		struct arsdk_device_conn *conn,
		void *userdata)
{
	stop_interfaces(device);
	device->info.state = device->deleted ?
		ARSDK_DEVICE_STATE_REMOVING : ARSDK_DEVICE_STATE_IDLE;
	(*device->cbs.disconnected)(device, &device->info,
			device->cbs.userdata);
	cleanup_connection(device);
}

/**
 */
static void canceled(struct arsdk_device *device,
		struct arsdk_device_conn *conn,
		enum arsdk_conn_cancel_reason reason,
		void *userdata)
{
	stop_interfaces(device);
	device->info.state = device->deleted ?
		ARSDK_DEVICE_STATE_REMOVING : ARSDK_DEVICE_STATE_IDLE;
	(*device->cbs.canceled)(device, &device->info,
			reason, device->cbs.userdata);
	cleanup_connection(device);
}

/**
 */
void arsdk_device_destroy(struct arsdk_device *self)
{
	if (self->conn != NULL)
		ARSDK_LOGW("device %p still connected during destroy", self);

	free(self->name);
	free(self->addr);
	free(self->id);
	free(self->json);
	free(self);
}

/**
 */
const char *arsdk_device_state_str(enum arsdk_device_state val)
{
	switch (val) {
	case ARSDK_DEVICE_STATE_IDLE: return "IDLE";
	case ARSDK_DEVICE_STATE_CONNECTING: return "CONNECTING";
	case ARSDK_DEVICE_STATE_CONNECTED: return "CONNECTED";
	case ARSDK_DEVICE_STATE_REMOVING: return "REMOVING";
	default: return "UNKNOWN";
	}
}

/**
 */
int arsdk_device_new(struct arsdkctrl_backend *backend,
		struct arsdk_discovery *discovery,
		int16_t discovery_runid,
		uint16_t handle,
		const struct arsdk_device_info *info,
		struct arsdk_device **ret_obj)
{
	struct arsdk_device *self = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(backend != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(info != NULL, -EINVAL);

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Initialize structure */
	self->backend = backend;
	self->discovery = discovery;
	self->handle = handle;

	/* Copy device info */
	self->info.backend_type = arsdkctrl_backend_get_type(self->backend);
	update_info(self, info);
	self->info.state = ARSDK_DEVICE_STATE_IDLE;
	self->discovery_runid = discovery_runid;
	*ret_obj = self;
	return 0;
}

/**
 */
int arsdk_device_clear_backend(struct arsdk_device *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	arsdk_device_disconnect(self);
	self->backend = NULL;
	return 0;
}

/**
 */
int arsdk_device_clear_discovery(struct arsdk_device *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	self->discovery = NULL;
	self->discovery_runid = -1;
	return 0;
}

/**
 */
int arsdk_device_set_osdata(struct arsdk_device *self, void *osdata)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	self->osdata = osdata;
	return 0;
}

/**
 */
void *arsdk_device_get_osdata(struct arsdk_device *self)
{
	return self == NULL ? NULL : self->osdata;
}

/**
 */
uint16_t arsdk_device_get_handle(struct arsdk_device *self)
{
	return self ? self->handle : ARSDK_INVALID_HANDLE;
}

/**
 */
int arsdk_device_get_info(struct arsdk_device *self,
		const struct arsdk_device_info **info)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(info != NULL, -EINVAL);
	*info = &self->info;
	return 0;
}

/**
 */
struct arsdkctrl_backend *arsdk_device_get_backend(struct arsdk_device *self)
{
	return self == NULL ? NULL : self->backend;
}

/**
 */
struct arsdk_discovery *arsdk_device_get_discovery(struct arsdk_device *self)
{
	return self == NULL ? NULL : self->discovery;
}

/**
 */
int16_t arsdk_device_get_discovery_runid(struct arsdk_device *dev)
{
	return dev ? dev->discovery_runid : -1;
}

/**
 */
int arsdk_device_set_discovery_runid(struct arsdk_device *dev, int16_t runid)
{
	ARSDK_RETURN_ERR_IF_FAILED(dev != NULL, -EINVAL);
	dev->discovery_runid = runid;
	return 0;
}

/**
 */
int arsdk_device_connect(struct arsdk_device *self,
		const struct arsdk_device_conn_cfg *cfg,
		const struct arsdk_device_conn_cbs *cbs,
		struct pomp_loop *loop)
{
	struct arsdk_device_conn_internal_cbs internal_cbs;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->connecting != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->connected != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->disconnected != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->canceled != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->link_status != NULL, -EINVAL);

	/* Must still be attached to a backend */
	if (self->backend == NULL)
		return -EPERM;

	/* Must no already have a connection in progress */
	if (self->conn != NULL)
		return -EPERM;

	/* Must no be deleted by manager */
	if (self->deleted)
		return -EPERM;

	/* Save callbacks */
	self->cbs = *cbs;

	/* Setup internal callbacks */
	memset(&internal_cbs, 0, sizeof(internal_cbs));
	internal_cbs.userdata = self;
	internal_cbs.connecting = &connecting;
	internal_cbs.connected = &connected;
	internal_cbs.disconnected = &disconnected;
	internal_cbs.canceled = &canceled;

	/* Forward to backend, internal callback will be notified afterwards */
	return arsdkctrl_backend_start_device_conn(self->backend, self,
			&self->info, cfg, &internal_cbs, loop, &self->conn);
}

/**
 */
int arsdk_device_disconnect(struct arsdk_device *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	/* Must still be attached to a backend */
	if (self->backend == NULL)
		return -EPERM;

	/* Nothing to do if no more connection in progress */
	if (self->conn == NULL)
		return 0;

	/* Forward to backend, internal callback will be notified afterwards */
	return arsdkctrl_backend_stop_device_conn(self->backend,
			self, self->conn);
}

/** TODO: ask a central database to get this */
static const struct arsdk_cmd_queue_info s_tx_info_table[] = {
	{
		.type = ARSDK_TRANSPORT_DATA_TYPE_NOACK,
		.id = ARSDK_TRANSPORT_ID_C2D_CMD_NOACK,
		.max_tx_rate_ms = 0,
		.ack_timeout_ms = -1,
		.default_max_retry_count = -1,
		.overwrite = 1,
	},
	{
		.type = ARSDK_TRANSPORT_DATA_TYPE_WITHACK,
		.id = ARSDK_TRANSPORT_ID_C2D_CMD_WITHACK,
		.max_tx_rate_ms = 0,
		.ack_timeout_ms = 150,
		.default_max_retry_count = 5,
		.overwrite = 0,
	},
	{
		.type = ARSDK_TRANSPORT_DATA_TYPE_WITHACK,
		.id = ARSDK_TRANSPORT_ID_C2D_CMD_HIGHPRIO,
		.max_tx_rate_ms = 0,
		.ack_timeout_ms = 150,
		.default_max_retry_count = INT32_MAX,
		.overwrite = 0,
	},
};

static const struct arsdk_cmd_queue_info s_tx_info_table_ble[] = {
{
		.type = ARSDK_TRANSPORT_DATA_TYPE_NOACK,
		.id = ARSDK_TRANSPORT_ID_C2D_CMD_NOACK,
		.max_tx_rate_ms = 0,
		.ack_timeout_ms = -1,
		.default_max_retry_count = -1,
		.overwrite = 1,
	},
	{
		.type = ARSDK_TRANSPORT_DATA_TYPE_WITHACK,
		.id = ARSDK_TRANSPORT_ID_C2D_CMD_WITHACK,
		.max_tx_rate_ms = 50,
		.ack_timeout_ms = 750,
		.default_max_retry_count = 5,
		.overwrite = 0,
	},
	{
		.type = ARSDK_TRANSPORT_DATA_TYPE_WITHACK,
		.id = ARSDK_TRANSPORT_ID_C2D_CMD_HIGHPRIO,
		.max_tx_rate_ms = 0,
		.ack_timeout_ms = 150,
		.default_max_retry_count = INT32_MAX,
		.overwrite = 0,
	},
};

/** */
static const uint32_t s_tx_count =
		sizeof(s_tx_info_table) / sizeof(s_tx_info_table[0]);
static const uint32_t s_tx_count_ble =
		sizeof(s_tx_info_table_ble) / sizeof(s_tx_info_table_ble[0]);

/**
 */
int arsdk_device_create_cmd_itf(
		struct arsdk_device *self,
		const struct arsdk_cmd_itf_cbs *cbs,
		struct arsdk_cmd_itf **ret_itf)
{
	int res = 0;
	struct arsdk_cmd_itf_internal_cbs internal_cbs;
	uint8_t ackoff = 0;
	const struct arsdk_cmd_queue_info *tx_info_table = NULL;
	uint32_t tx_count = 0;

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

	/* Determine parameters depending on backend type */
	if (arsdkctrl_backend_get_type(self->backend) ==
			ARSDK_BACKEND_TYPE_BLE) {
		tx_info_table = &s_tx_info_table_ble[0];
		tx_count = s_tx_count_ble;
		ackoff = ARSDK_TRANSPORT_ID_ACKOFF_BLE;
	} else {
		tx_info_table = &s_tx_info_table[0];
		tx_count = s_tx_count;
		ackoff = ARSDK_TRANSPORT_ID_ACKOFF;
	}

	/* Create command interface */
	memset(&internal_cbs, 0, sizeof(internal_cbs));
	internal_cbs.userdata = self;
	internal_cbs.dispose = &cmd_itf_dispose;

	res = arsdk_cmd_itf_new(
			self->transport,
			cbs, &internal_cbs,
			tx_info_table, tx_count, ackoff,
			ret_itf);
	if (res == 0) {
		/* Keep it */
		self->cmd_itf = *ret_itf;
	}

	return res;
}

/**
 */
struct arsdk_cmd_itf *arsdk_device_get_cmd_itf(
		struct arsdk_device *self) {
	if (!self)
		return NULL;
	return self->cmd_itf;
}

/**
 */
int arsdk_device_get_ftp_itf(
		struct arsdk_device *self,
		struct arsdk_ftp_itf **ret_itf)
{
	int res = 0;
	struct arsdk_ftp_itf_internal_cbs internal_cbs;
	struct mux_ctx *mux = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_itf != NULL, -EINVAL);
	*ret_itf = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	if (self->backend == NULL)
		return -EPERM;
	if (self->transport == NULL)
		return -EPERM;
	if (self->ftp_itf != NULL) {
		*ret_itf = self->ftp_itf;
		return 0;
	}

	if (arsdkctrl_backend_get_type(self->backend) ==
			ARSDK_BACKEND_TYPE_MUX) {
		mux = arsdk_backend_mux_get_mux_ctx(
				arsdkctrl_backend_get_child(self->backend));
	}

	/* Create ftp interface */
	memset(&internal_cbs, 0, sizeof(internal_cbs));
	internal_cbs.userdata = self;
	internal_cbs.dispose = &ftp_itf_dispose;
	internal_cbs.socketcb = &ftp_itf_socket_cb;
	res = arsdk_ftp_itf_new(
			self->transport,
			&internal_cbs,
			&self->info,
			mux,
			ret_itf);
	if (res == 0) {
		/* Keep it */
		self->ftp_itf = *ret_itf;
	}

	return res;
}

/**
 */
int arsdk_device_get_media_itf(
		struct arsdk_device *self,
		struct arsdk_media_itf **ret_itf)
{
	struct arsdk_ftp_itf *ftp_itf = NULL;
	int res = 0;

	ARSDK_RETURN_ERR_IF_FAILED(ret_itf != NULL, -EINVAL);
	*ret_itf = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	if (self->backend == NULL)
		return -EPERM;
	if (self->transport == NULL)
		return -EPERM;
	if (self->media_itf != NULL) {
		*ret_itf = self->media_itf;
		return 0;
	}

	if (self->ftp_itf == NULL) {
		res = arsdk_device_get_ftp_itf(
				self,
				&ftp_itf);
		if (res < 0)
			return res;
	} else {
		ftp_itf = self->ftp_itf;
	}

	/* Create media interface */
	res = arsdk_media_itf_new(ftp_itf, ret_itf);
	if (res == 0) {
		/* Keep it */
		self->media_itf = *ret_itf;
	}

	return res;
}

/**
 */
int arsdk_device_get_updater_itf(
		struct arsdk_device *self,
		struct arsdk_updater_itf **ret_itf)
{
	int res = 0;
	struct arsdk_ftp_itf *ftp_itf = NULL;
	struct mux_ctx *mux = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_itf != NULL, -EINVAL);
	*ret_itf = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	if (self->backend == NULL)
		return -EPERM;
	if (self->transport == NULL)
		return -EPERM;
	if (self->updater_itf != NULL) {
		*ret_itf = self->updater_itf;
		return 0;
	}

	res = arsdk_device_get_ftp_itf(self, &ftp_itf);
	if (res < 0)
		return res;

	if (arsdkctrl_backend_get_type(self->backend) ==
			ARSDK_BACKEND_TYPE_MUX) {
		mux = arsdk_backend_mux_get_mux_ctx(
				arsdkctrl_backend_get_child(self->backend));
	}

	/* Create updater interface */
	res = arsdk_updater_itf_new(&self->info, ftp_itf, mux, ret_itf);
	if (res == 0) {
		/* Keep it */
		self->updater_itf = *ret_itf;
	}

	return res;
}

int arsdk_device_get_blackbox_itf(
		struct arsdk_device *self,
		struct arsdk_blackbox_itf **ret_itf)
{
	int res = 0;
	struct mux_ctx *mux = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_itf != NULL, -EINVAL);
	*ret_itf = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	if (self->backend == NULL)
		return -EPERM;
	if (self->blackbox_itf != NULL) {
		*ret_itf = self->blackbox_itf;
		return 0;
	}

	if (arsdkctrl_backend_get_type(self->backend) ==
			ARSDK_BACKEND_TYPE_MUX) {
		mux = arsdkctrl_backend_mux_get_mux_ctx(
				arsdkctrl_backend_get_child(self->backend));
	}

	/* Create blackbox interface */
	res = arsdk_blackbox_itf_new(mux, ret_itf);
	if (res == 0) {
		/* Keep it */
		self->blackbox_itf = *ret_itf;
	}

	return res;
}

/**
 */
int arsdk_device_get_crashml_itf(
		struct arsdk_device *self,
		struct arsdk_crashml_itf **ret_itf)
{
	int res = 0;
	struct arsdk_ftp_itf *ftp_itf = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_itf != NULL, -EINVAL);
	*ret_itf = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	if (self->crashml_itf != NULL) {
		*ret_itf = self->crashml_itf;
		return 0;
	}

	res = arsdk_device_get_ftp_itf(self, &ftp_itf);
	if (res < 0)
		return res;

	/* Create crashml interface */
	res = arsdk_crashml_itf_new(&self->info, ftp_itf, ret_itf);
	if (res == 0) {
		/* Keep it */
		self->crashml_itf = *ret_itf;
	}

	return res;
}

/**
 */
int arsdk_device_get_flight_log_itf(
		struct arsdk_device *self,
		struct arsdk_flight_log_itf **ret_itf)
{
	int res = 0;
	struct arsdk_ftp_itf *ftp_itf = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_itf != NULL, -EINVAL);
	*ret_itf = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	if (self->flight_log_itf != NULL) {
		*ret_itf = self->flight_log_itf;
		return 0;
	}

	res = arsdk_device_get_ftp_itf(self, &ftp_itf);
	if (res < 0)
		return res;

	/* Create flight log interface */
	res = arsdk_flight_log_itf_new(&self->info, ftp_itf, ret_itf);
	if (res == 0) {
		/* Keep it */
		self->flight_log_itf = *ret_itf;
	}

	return res;
}

/**
 */
int arsdk_device_get_pud_itf(
		struct arsdk_device *self,
		struct arsdk_pud_itf **ret_itf)
{
	int res = 0;
	struct arsdk_ftp_itf *ftp_itf = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_itf != NULL, -EINVAL);
	*ret_itf = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	if (self->pud_itf != NULL) {
		*ret_itf = self->pud_itf;
		return 0;
	}

	res = arsdk_device_get_ftp_itf(self, &ftp_itf);
	if (res < 0)
		return res;

	/* Create pud interface */
	res = arsdk_pud_itf_new(&self->info, ftp_itf, ret_itf);
	if (res == 0) {
		/* Keep it */
		self->pud_itf = *ret_itf;
	}

	return res;
}

/**
 */
int arsdk_device_get_ephemeris_itf(
		struct arsdk_device *self,
		struct arsdk_ephemeris_itf **ret_itf)
{
	int res = 0;
	struct arsdk_ftp_itf *ftp_itf = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_itf != NULL, -EINVAL);
	*ret_itf = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	if (self->ephemeris_itf != NULL) {
		*ret_itf = self->ephemeris_itf;
		return 0;
	}

	res = arsdk_device_get_ftp_itf(self, &ftp_itf);
	if (res < 0)
		return res;

	/* Create ephemeris interface */
	res = arsdk_ephemeris_itf_new(&self->info, ftp_itf, ret_itf);
	if (res == 0) {
		/* Keep it */
		self->ephemeris_itf = *ret_itf;
	}

	return res;
}

static int resolution(struct arsdk_device_info *dev_info,
		enum arsdk_device_type dev_type,
		int *port,
		const char **host)
{
	ARSDK_RETURN_ERR_IF_FAILED(dev_info != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(port != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(host != NULL, -EINVAL);

	switch (dev_info->backend_type) {
	case ARSDK_BACKEND_TYPE_NET:
		if (dev_type != dev_info->type &&
		    dev_info->type != ARSDK_DEVICE_TYPE_SKYCTRL)
			*port += 100;

		*host = dev_info->addr;
		return 0;
	case ARSDK_BACKEND_TYPE_MUX:
		if (dev_type == ARSDK_DEVICE_TYPE_SKYCTRL_2 ||
		    dev_type == ARSDK_DEVICE_TYPE_SKYCTRL_2P ||
		    dev_type == ARSDK_DEVICE_TYPE_SKYCTRL_NG ||
		    dev_type == ARSDK_DEVICE_TYPE_SKYCTRL_3)
			*host = "skycontroller";
		else
			*host = "drone";
		return 0;
	case ARSDK_BACKEND_TYPE_BLE:
		return -EPERM;
	case ARSDK_BACKEND_TYPE_UNKNOWN:
	default:
		return -EINVAL;
	}
}

int arsdk_device_create_tcp_proxy(struct arsdk_device *self,
		enum arsdk_device_type dev_type,
		uint16_t port,
		struct arsdk_device_tcp_proxy **ret_proxy)
{
	int res = 0;
	struct mux_ctx *mux = NULL;
	struct arsdk_device_tcp_proxy *proxy = NULL;
	const char *res_host = NULL;
	int tmp_port = port;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_proxy != NULL, -EINVAL);

	proxy = calloc(1, sizeof(*proxy));
	if (proxy == NULL)
		return -ENOMEM;

	proxy->device = self;

	if (arsdkctrl_backend_get_type(self->backend) ==
			ARSDK_BACKEND_TYPE_MUX) {
		mux = arsdk_backend_mux_get_mux_ctx(
				arsdkctrl_backend_get_child(self->backend));
	}

	res = resolution(&self->info, dev_type, &tmp_port, &res_host);
	if (res < 0)
		return res;

	if (mux == NULL) {
		proxy->port = tmp_port;
		proxy->addr = strdup(res_host);
	} else {
		/* Allocate mux channel */
		res = mux_tcp_proxy_new(mux, res_host, tmp_port, 0,
				&proxy->mux_tcp_proxy);
		if (res < 0) {
			ARSDK_LOG_ERRNO("mux_channel_open_tcp", -res);
			goto error;
		}
		proxy->addr = strdup("127.0.0.1");
		proxy->port = mux_tcp_proxy_get_port(proxy->mux_tcp_proxy);
	}

	*ret_proxy = proxy;
	return 0;
error:
	arsdk_device_destroy_tcp_proxy(proxy);
	return res;
}

int arsdk_device_destroy_tcp_proxy(struct arsdk_device_tcp_proxy *proxy)
{
	int res = 0;

	ARSDK_RETURN_ERR_IF_FAILED(proxy != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(proxy->device != NULL, -EINVAL);

	res = mux_tcp_proxy_destroy(proxy->mux_tcp_proxy);
	if (res < 0)
		ARSDK_LOG_ERRNO("mux_channel_close", -res);

	free(proxy->addr);
	free(proxy);

	return 0;
}

const char *arsdk_device_tcp_proxy_get_addr(
		struct arsdk_device_tcp_proxy *proxy)
{
	return (proxy == NULL) ? NULL : proxy->addr;
}

int arsdk_device_tcp_proxy_get_port(struct arsdk_device_tcp_proxy *proxy)
{
	return (proxy == NULL) ? EINVAL : proxy->port;
}
