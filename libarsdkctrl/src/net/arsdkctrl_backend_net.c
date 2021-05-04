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

/* ulog requires 1 source file to declare the log tag */
#ifdef BUILD_LIBULOG
ULOG_DECLARE_TAG(arsdkctrl_net);
#endif /* BUILD_LIBULOG */

/** */
enum device_conn_state {
	DEVICE_CONN_STATE_IDLE,
	DEVICE_CONN_STATE_CONNECTING_JSON,
	DEVICE_CONN_STATE_JSON_SENT,
	DEVICE_CONN_STATE_JSON_RECEIVED,
	DEVICE_CONN_STATE_CONNECTED,
};

/** */
struct arsdk_device_conn {
	struct arsdk_device                    *device;
	struct arsdk_device_conn_internal_cbs  cbs;
	enum device_conn_state                 state;
	struct pomp_loop                       *loop;
	struct pomp_ctx                        *ctx;
	struct arsdk_transport_net             *transport;
	char                                   *ctrl_name;
	char                                   *ctrl_type;
	char                                   *device_id;
	char                                   *txjson;
	char                                   *rxjson;
	int                                    status;
	in_addr_t                              in_addr;
	uint16_t                               d2c_data_port;
	uint16_t                               c2d_data_port;
	int                                    qos_mode_supported;
	int                                    qos_mode;
	int                                    stream_supported;
	/** minimum protocol version supported */
	uint32_t                               proto_v_min;
	/** maximum protocol version supported */
	uint32_t                               proto_v_max;
	/** protocol version used */
	uint32_t                               proto_v;
};

/** */
struct arsdkctrl_backend_net {
	struct arsdkctrl_backend               *parent;
	struct pomp_loop                       *loop;
	char                                   *iface;
	/* Function to call when sockets are created */
	arsdkctrl_backend_net_socket_cb_t      socketcb;
	/* User data for socket hook callback */
	void                                   *userdata;

	int                                    qos_mode_supported;
	int                                    stream_supported;
	/** minimum protocol version supported */
	uint32_t                               proto_v_min;
	/** maximum protocol version supported */
	uint32_t                               proto_v_max;
};

static void arsdkctrl_backend_net_socket_cb(struct arsdkctrl_backend *base,
		int fd, enum arsdk_socket_kind kind)
{
	struct arsdkctrl_backend_net *self =
			arsdkctrl_backend_get_child(base);

	ARSDK_RETURN_IF_FAILED(self != NULL, -EINVAL);

	/* socket hook callback */
	if (self->socketcb != NULL)
		(*self->socketcb)(self, fd, kind, self->userdata);
}

/** */
static void backend_ctrl_net_socket_cb(struct arsdk_transport_net *self,
		int fd,
		enum arsdk_socket_kind kind,
		void *userdata)
{
	struct arsdkctrl_backend_net *backend_net = userdata;

	ARSDK_RETURN_IF_FAILED(backend_net != NULL, -EINVAL);

	/* socket hook callback */
	arsdkctrl_backend_net_socket_cb(backend_net->parent, fd, kind);
}

/**
 */
static int parse_port(json_object *jroot, const char *key, uint16_t *port)
{
	json_object *jport = NULL;
	int value = 0;

	jport = get_json_object(jroot, key);
	if (jport == NULL)
		return -EINVAL;

	value = json_object_get_int(jport);
	if (value <= 0 || value > 65535) {
		ARSDK_LOGE("Invalid %s port: %d", key, value);
		return -EINVAL;
	}

	*port = (uint16_t)value;
	return 0;
}

/**
 */
static int device_conn_setup_transport(struct arsdk_device_conn *self,
		struct arsdkctrl_backend_net *backend_net)
{
	int res = 0;
	struct arsdk_transport_net_cfg cfg;
	struct arsdk_transport_net_cbs transport_net_cbs;

	memset(&cfg, 0, sizeof(cfg));
	cfg.data.rx_port = ARSDK_NET_DEFAULT_D2C_DATA_PORT;
	cfg.stream_supported = backend_net->stream_supported;
	cfg.proto_v = self->proto_v;

	/* Create transport */
	memset(&transport_net_cbs, 0, sizeof(transport_net_cbs));
	transport_net_cbs.userdata = backend_net;
	transport_net_cbs.socketcb = &backend_ctrl_net_socket_cb;
	res = arsdk_transport_net_new(self->loop, &cfg,
			&transport_net_cbs, &self->transport);
	if (res < 0)
		goto error;

	/* Retrieve bound ports */
	res = arsdk_transport_net_get_cfg(self->transport, &cfg);
	if (res < 0)
		goto error;

	/* Success */
	self->d2c_data_port = cfg.data.rx_port;
	return 0;

	/* Cleanup in case of error */
error:
	if (self->transport != NULL) {
		arsdk_transport_destroy(arsdk_transport_net_get_parent(
				self->transport));
		self->transport = NULL;
	}
	return res;
}

/**
 */
static int device_conn_send_json(struct arsdk_device_conn *self,
		struct pomp_conn *conn)
{
	int res = 0;
	json_object *jroot = NULL;
	const char *newjson = NULL;
	struct pomp_buffer *buf = NULL;

	/* Parse given json */
	if (self->txjson != NULL)
		jroot = json_tokener_parse(self->txjson);
	else
		jroot = json_object_new_object();
	if (jroot == NULL) {
		res = -EINVAL;
		goto out;
	}

	/* Add name */
	if (self->ctrl_name != NULL) {
		json_object_object_add(jroot,
				ARSDK_CONN_JSON_KEY_CONTROLLER_NAME,
			json_object_new_string(self->ctrl_name));
	}

	/* Add type */
	if (self->ctrl_type != NULL) {
		json_object_object_add(jroot,
				ARSDK_CONN_JSON_KEY_CONTROLLER_TYPE,
			json_object_new_string(self->ctrl_type));
	}

	/* Add data port */
	json_object_object_add(jroot, ARSDK_CONN_JSON_KEY_D2CPORT,
			json_object_new_int(self->d2c_data_port));

	/* Add device id */
	if (self->device_id != NULL) {
		json_object_object_add(jroot,
				ARSDK_CONN_JSON_KEY_DEVICE_ID,
			json_object_new_string(self->device_id));
	}

	/* Add requested QOS mode */
	json_object_object_add(jroot, ARSDK_CONN_JSON_KEY_QOS_MODE,
			json_object_new_int(self->qos_mode_supported));

	/* Add supported protocol versions */
	json_object_object_add(jroot, ARSDK_CONN_JSON_KEY_PROTO_V_MIN,
			json_object_new_int(self->proto_v_min));
	json_object_object_add(jroot, ARSDK_CONN_JSON_KEY_PROTO_V_MAX,
			json_object_new_int(self->proto_v_max));

	/* Get updated json */
	newjson = json_object_to_json_string(jroot);
	if (newjson == NULL) {
		res = -ENOMEM;
		goto out;
	}
	ARSDK_LOGI("Sending json:");
	ARSDK_LOGI_STR(newjson);

	/* Create buffer with json content */
	buf = pomp_buffer_new_with_data(newjson, strlen(newjson));
	if (buf == NULL)
		return -ENOMEM;

	/* Send buffer */
	res = pomp_conn_send_raw_buf(conn, buf);

out:
	if (jroot != NULL)
		json_object_put(jroot);
	if (buf != NULL)
		pomp_buffer_unref(buf);
	return res;
}

/**
 */
static int parse_qos_mode(json_object *object)
{
	int qos_mode;
	json_object *jqos_mode = NULL;

	if (!object)
		return 0;

	/* QOS disabled by default (in particular if not present) */
	qos_mode = 0;

	jqos_mode = get_json_object(object, ARSDK_CONN_JSON_KEY_QOS_MODE);
	if (jqos_mode != NULL)
		qos_mode = json_object_get_int(jqos_mode);

	/* QOS disabled if invalid value */
	if (qos_mode < 0)
		qos_mode = 0;

	return qos_mode;
}

/**
 */
static uint32_t parse_proto_version(json_object *object)
{
	/* by default only the protocol version 1 is considered as supported */
	uint32_t proto_v = ARSDK_PROTOCOL_VERSION_1;
	json_object *jproto_v = NULL;

	if (!object)
		return 1;

	jproto_v = get_json_object(object, ARSDK_CONN_JSON_KEY_PROTO_V);
	if (jproto_v != NULL)
		proto_v = json_object_get_int(jproto_v);

	return proto_v;
}

/**
 */
static int device_conn_recv_json(struct arsdk_device_conn *self,
		struct pomp_buffer *buf)
{
	const void *cdata = NULL;
	size_t len = 0;
	json_object *jroot = NULL;
	json_object *jstatus = NULL;

	/* Get data from buffer */
	pomp_buffer_get_cdata(buf, &cdata, &len, NULL);

	/* Copy full json (and make it null terminated) */
	self->rxjson = calloc(1, len + 1);
	if (self->rxjson == NULL)
		return -ENOMEM;
	memcpy(self->rxjson, cdata, len);
	self->rxjson[len] = '\0';

	ARSDK_LOGI("Received json:");
	ARSDK_LOGI_STR(self->rxjson);

	/* Parse json request */
	jroot = json_tokener_parse(self->rxjson);
	if (jroot == NULL)
		goto error;

	/* Parse connection status */
	jstatus = get_json_object(jroot, ARSDK_CONN_JSON_KEY_STATUS);
	if (jstatus == NULL)
		goto error;
	self->status = json_object_get_int(jstatus);
	if (self->status != 0) {
		/* If the connection is refused only status is needed */
		goto end;
	}

	/* Parse data port number */
	if (parse_port(jroot,
			ARSDK_CONN_JSON_KEY_C2DPORT,
			&self->c2d_data_port) < 0) {
		goto error;
	}

	/* Parse the chosen QOS mode:
	 * if not present QOS is unsupported by the device */
	self->qos_mode = parse_qos_mode(jroot);

	self->proto_v = parse_proto_version(jroot);

end:
	/* Success */
	json_object_put(jroot);
	return 0;

	/* Cleanup in case of error */
error:
	ARSDK_LOGE("Failed to parse json response: '%s'", self->rxjson);
	if (jroot != NULL)
		json_object_put(jroot);
	free(self->rxjson);
	self->rxjson = NULL;
	return -EINVAL;
}

/**
 */
static void device_conn_destroy(struct arsdk_device_conn *self)
{
	int res = 0;

	/* Stop and destroy transport */
	if (self->transport != NULL) {
		res = arsdk_transport_stop(arsdk_transport_net_get_parent(
				self->transport));
		if (res < 0)
			ARSDK_LOG_ERRNO("arsdk_transport_stop", -res);
		res = arsdk_transport_destroy(arsdk_transport_net_get_parent(
				self->transport));
		if (res < 0)
			ARSDK_LOG_ERRNO("arsdk_transport_destroy", -res);
	}

	/* Stop and destroy pomp context used for connection request */
	if (self->ctx != NULL) {
		res = pomp_ctx_stop(self->ctx);
		if (res < 0)
			ARSDK_LOG_ERRNO("pomp_ctx_stop", -res);
		res = pomp_ctx_destroy(self->ctx);
		if (res < 0)
			ARSDK_LOG_ERRNO("pomp_ctx_stop", -res);
	}

	free(self->ctrl_name);
	free(self->ctrl_type);
	free(self->txjson);
	free(self->rxjson);
	free(self);
}

/**
 */
static void device_conn_idle_destroy(void *userdata)
{
	struct arsdk_device_conn *self = userdata;
	device_conn_destroy(self);
}

/**
 */
static void device_conn_event_cb(
		struct pomp_ctx *ctx,
		enum pomp_event event,
		struct pomp_conn *conn,
		const struct pomp_msg *msg,
		void *userdata)
{
	struct arsdk_device_conn *self = userdata;

	switch (event) {
	case POMP_EVENT_CONNECTED:
		/* Send json and wait for answer or disconnect immediately to
		 * trigger reconnection */
		if (device_conn_send_json(self, conn) == 0)
			self->state = DEVICE_CONN_STATE_JSON_SENT;
		else
			pomp_conn_disconnect(conn);
		break;

	case POMP_EVENT_DISCONNECTED:
		/* If json was correctly received, we can stop context
		 * Otherwise, wait for reconnection */
		if (self->state >= DEVICE_CONN_STATE_JSON_RECEIVED)
			pomp_ctx_stop(self->ctx);
		else
			self->state = DEVICE_CONN_STATE_CONNECTING_JSON;
		break;

	case POMP_EVENT_MSG:
		/* Not received for raw context */
		break;

	default:
		break;
	}
}

/**
 */
static void device_conn_raw_cb(
		struct pomp_ctx *ctx,
		struct pomp_conn *conn,
		struct pomp_buffer *buf,
		void *userdata)
{
	int res = 0;
	struct arsdk_device_conn *self = userdata;
	const struct arsdk_device_info *info = NULL;
	struct arsdk_device_info newinfo;
	struct arsdk_transport_net_cfg cfg;

	/* First of all, disconnect and change state*/
	pomp_conn_disconnect(conn);
	self->state = DEVICE_CONN_STATE_JSON_RECEIVED;

	/* Parse received json */
	res = device_conn_recv_json(self, buf);
	if (res < 0)
		goto error;

	if (self->status != 0) {
		ARSDK_LOGI("Connection refused");
		goto rejected;
	}

	/* Check the protocol version */
	if (self->proto_v < self->proto_v_min &&
	    self->proto_v > self->proto_v_max) {
		ARSDK_LOGI("Bad protocol version (%d) not supported",
				self->proto_v);
		goto rejected;
	}

	/* Setup tx address and ports for transport layer */
	memset(&cfg, 0, sizeof(cfg));
	res = arsdk_transport_net_get_cfg(self->transport, &cfg);
	if (res < 0)
		goto error;
	cfg.tx_addr = self->in_addr;
	cfg.qos_mode = self->qos_mode;
	cfg.data.tx_port = self->c2d_data_port;
	cfg.proto_v = self->proto_v;
	res = arsdk_transport_net_update_cfg(self->transport, &cfg);
	if (res < 0)
		goto error;

	/* Update info */
	res = arsdk_device_get_info(self->device, &info);
	if (res < 0)
		goto error;

	newinfo = *info;
	newinfo.proto_v = self->proto_v;
	newinfo.api = ARSDK_DEVICE_API_FULL;
	newinfo.json = self->rxjson;

	/* Notify connection */
	self->state = DEVICE_CONN_STATE_CONNECTED;
	(*self->cbs.connected)(self->device, &newinfo, self,
			arsdk_transport_net_get_parent(self->transport),
			self->cbs.userdata);
	return;

error:
	/* Start again */
	self->state = DEVICE_CONN_STATE_CONNECTING_JSON;
	return;

rejected:
	/* Notify rejection */
	(*self->cbs.canceled)(self->device, self,
			ARSDK_CONN_CANCEL_REASON_REJECTED,
			self->cbs.userdata);
	pomp_loop_idle_add(self->loop, &device_conn_idle_destroy, self);
}

/**
 */
static void device_conn_pomp_socket_cb(
		struct pomp_ctx *ctx,
		int fd,
		enum pomp_socket_kind kind,
		void *userdata)
{
	struct arsdk_device_conn *self = userdata;
	struct arsdk_transport_net *transport_net = NULL;

	ARSDK_RETURN_IF_FAILED(self != NULL, -EINVAL);

	transport_net = self->transport;

	ARSDK_RETURN_IF_FAILED(transport_net != NULL, -EINVAL);

	/* socket hook callback */
	arsdk_transport_net_socket_cb(transport_net, fd,
			ARSDK_SOCKET_KIND_CONNECTION);
}

/**
 */
static int device_conn_new(
		struct arsdk_device *device,
		const struct arsdk_device_conn_cfg *cfg,
		const struct arsdk_device_conn_internal_cbs *cbs,
		struct pomp_loop *loop,
		uint32_t proto_v_max, uint32_t proto_v_min,
		int qos_mode_supported,
		int stream_supported,
		struct arsdk_device_conn **ret_conn)
{
	int res = 0;
	struct arsdk_device_conn *self = NULL;

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Save device */
	self->device = device;

	/* Copy connection config, initialize state */
	self->loop = loop;
	self->ctrl_name = xstrdup(cfg->ctrl_name);
	self->ctrl_type = xstrdup(cfg->ctrl_type);
	self->device_id = xstrdup(cfg->device_id);
	self->txjson = xstrdup(cfg->json);
	self->cbs = *cbs;
	self->qos_mode_supported = qos_mode_supported;
	self->stream_supported = stream_supported;
	self->state = DEVICE_CONN_STATE_IDLE;
	self->proto_v_min = proto_v_min;
	self->proto_v_max = proto_v_max;

	/* Create pomp context, make it raw */
	self->ctx = pomp_ctx_new_with_loop(&device_conn_event_cb, self, loop);
	if (self->ctx == NULL) {
		res = -ENOMEM;
		goto error;
	}
	res = pomp_ctx_set_raw(self->ctx, &device_conn_raw_cb);
	if (res < 0)
		goto error;

	/* Disable TCP keepalive */
	res = pomp_ctx_setup_keepalive(self->ctx, 0, 0, 0, 0);
	if (res < 0)
		goto error;

	/* Set socket hook callback */
	res = pomp_ctx_set_socket_cb(self->ctx, &device_conn_pomp_socket_cb);
	if (res < 0) {
		ARSDK_LOG_ERRNO("pomp_ctx_set_socket_cb", -res);
		goto error;
	}

	*ret_conn = self;
	return 0;

	/* Cleanup in case of error */
error:
	device_conn_destroy(self);
	return res;
};

/**
 */
static int arsdkctrl_backend_net_start_device_conn(
		struct arsdkctrl_backend *base,
		struct arsdk_device *device,
		struct arsdk_device_info *info,
		const struct arsdk_device_conn_cfg *cfg,
		const struct arsdk_device_conn_internal_cbs *cbs,
		struct pomp_loop *loop,
		struct arsdk_device_conn **ret_conn)
{
	int res = 0;
	struct arsdkctrl_backend_net *self =
			arsdkctrl_backend_get_child(base);
	struct arsdk_device_conn *conn = NULL;
	struct sockaddr_in addr;

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

	/* Create device connection context */
	res = device_conn_new(device, cfg, cbs, loop,
			self->proto_v_max, self->proto_v_min,
			self->qos_mode_supported,
			self->stream_supported,
			&conn);
	if (res < 0)
		goto error;

	/* Setup transport (to get udp rx port) */
	res = device_conn_setup_transport(conn, self);
	if (res < 0)
		goto error;

	/* Setup address for tcp connection request */
	memset(&addr,  0, sizeof(addr));
	addr.sin_family = AF_INET;
	/* TODO: check validity of address */
	addr.sin_addr.s_addr = inet_addr(info->addr);
	addr.sin_port = htons(info->port);
	conn->in_addr = ntohl(addr.sin_addr.s_addr);

	/* Start connecting */
	conn->state = DEVICE_CONN_STATE_CONNECTING_JSON;
	res = pomp_ctx_connect(conn->ctx,
			(const struct sockaddr *)&addr, sizeof(addr));
	if (res < 0) {
		ARSDK_LOG_ERRNO("pomp_ctx_connect", -res);
		goto error;
	}

	/* Success */
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
static int arsdkctrl_backend_net_stop_device_conn(
		struct arsdkctrl_backend *base,
		struct arsdk_device *device,
		struct arsdk_device_conn *conn)
{
	struct arsdkctrl_backend_net *self =
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
	return 0;
}

/**
 */
static const struct arsdkctrl_backend_ops s_arsdkctrl_backend_net_ops = {
	.start_device_conn = &arsdkctrl_backend_net_start_device_conn,
	.stop_device_conn = &arsdkctrl_backend_net_stop_device_conn,
	.socket_cb = &arsdkctrl_backend_net_socket_cb,
};

/**
 */
int arsdkctrl_backend_net_new(struct arsdk_ctrl *ctrl,
		const struct arsdkctrl_backend_net_cfg *cfg,
		struct arsdkctrl_backend_net **ret_obj)
{
	int res = 0;
	struct arsdkctrl_backend_net *self = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(cfg != NULL, -EINVAL);
#if ARSDKCTRL_BACKEND_NET_PROTO_MIN > 1
	ARSDK_RETURN_ERR_IF_FAILED(cfg->proto_v_min == 0 ||
			cfg->proto_v_min >= ARSDKCTRL_BACKEND_NET_PROTO_MIN,
			-EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg->proto_v_max == 0 ||
			cfg->proto_v_max >= ARSDKCTRL_BACKEND_NET_PROTO_MIN,
			-EINVAL);
#endif
	ARSDK_RETURN_ERR_IF_FAILED(cfg->proto_v_max == 0 ||
			cfg->proto_v_max <= ARSDKCTRL_BACKEND_NET_PROTO_MAX,
			-EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(
			(cfg->proto_v_max != 0 &&
			 cfg->proto_v_min <= cfg->proto_v_max) ||
			(cfg->proto_v_max == 0 &&
			 cfg->proto_v_min <= ARSDKCTRL_BACKEND_NET_PROTO_MAX),
			-EINVAL);

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Setup base structure */
	res = arsdkctrl_backend_new(self, ctrl, "net", ARSDK_BACKEND_TYPE_NET,
			&s_arsdkctrl_backend_net_ops, &self->parent);
	if (res < 0)
		goto error;

	/* Initialize structure */
	self->loop = arsdk_ctrl_get_loop(ctrl);
	self->iface = xstrdup(cfg->iface);
	self->qos_mode_supported = cfg->qos_mode_supported;
	self->stream_supported = cfg->stream_supported;
	/* by default all protocol versions implemented are supported */
	self->proto_v_min = cfg->proto_v_min != 0 ? cfg->proto_v_min :
			ARSDKCTRL_BACKEND_NET_PROTO_MIN;
	self->proto_v_max = cfg->proto_v_max != 0 ? cfg->proto_v_max :
			ARSDKCTRL_BACKEND_NET_PROTO_MAX;

	/* Success */
	*ret_obj = self;
	return 0;

	/* TODO: Cleanup in case of error */
error:
	return res;
}

/**
 */
int arsdkctrl_backend_net_destroy(struct arsdkctrl_backend_net *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	/* destroy backend */
	arsdkctrl_backend_destroy(self->parent);

	/* Free resources */
	free(self->iface);
	free(self);
	return 0;
}

int arsdkctrl_backend_net_set_socket_cb(struct arsdkctrl_backend_net *self,
		arsdkctrl_backend_net_socket_cb_t cb, void *userdata)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	self->socketcb = cb;
	self->userdata = userdata;

	return 0;
}


/**
 */
struct arsdkctrl_backend *arsdkctrl_backend_net_get_parent(
		struct arsdkctrl_backend_net *self)
{
	return self ? self->parent : NULL;
}
