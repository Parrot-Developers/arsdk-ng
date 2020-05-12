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

/* ulog requires 1 source file to declare the log tag */
#ifdef BUILD_LIBULOG
ULOG_DECLARE_TAG(arsdk_net);
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
enum peer_conn_state {
	PEER_CONN_STATE_PENDING,
	PEER_CONN_STATE_CONNECTED,
};

/** */
struct arsdk_peer_conn {
	struct arsdk_peer                      *peer;
	struct arsdk_backend_net               *backend;
	struct arsdk_peer_conn_internal_cbs    cbs;
	enum peer_conn_state                   state;
	struct pomp_loop                       *loop;
	struct pomp_conn                       *conn;
	struct arsdk_transport_net             *transport;
	in_addr_t                              in_addr;
	uint16_t                               d2c_data_port;
	uint16_t                               c2d_data_port;
	uint16_t                               d2c_rtp_port;
	uint16_t                               c2d_rtp_port;
	uint16_t                               d2c_rtcp_port;
	uint16_t                               c2d_rtcp_port;
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
struct arsdk_backend_net {
	struct arsdk_backend                   *parent;
	struct pomp_loop                       *loop;
	char                                   *iface;
	/* Function to call when sockets are created */
	arsdk_backend_net_socket_cb_t          socketcb;
	/* User data for socket hook callback */
	void                                   *userdata;

	/** minimum protocol version supported */
	uint32_t                               proto_v_min;
	/** maximum protocol version supported */
	uint32_t                               proto_v_max;

	int                                    qos_mode_supported;
	int                                    stream_supported;

	struct {
		struct pomp_ctx                     *ctx;
		struct arsdk_backend_listen_cbs     cbs;
		struct arsdk_peer_conn              *conn;
	} listen;
};

/** */
struct arsdk_req {
	char      *ctrl_name;
	char      *ctrl_type;
	uint16_t  d2c_data_port;
	uint16_t  d2c_rtp_port;
	uint16_t  d2c_rtcp_port;
	char      *device_id;
	char      *json;
	int       qos_mode;
	/** minimum protocol version supported */
	uint32_t  proto_v_min;
	/** maximum protocol version supported */
	uint32_t  proto_v_max;
};

static void arsdk_backend_net_socket_cb(struct arsdk_backend *base, int fd,
		enum arsdk_socket_kind kind)
{
	struct arsdk_backend_net *self = arsdk_backend_get_child(base);

	ARSDK_RETURN_IF_FAILED(self != NULL, -EINVAL);

	/* socket hook callback */
	if (self->socketcb != NULL)
		(*self->socketcb)(self, fd, kind, self->userdata);
}

/** */
static void backend_net_socket_cb(struct arsdk_transport_net *self,
		int fd,
		enum arsdk_socket_kind kind,
		void *userdata)
{
	struct arsdk_backend_net *backend_net = userdata;

	ARSDK_RETURN_IF_FAILED(backend_net != NULL, -EINVAL);

	/* socket hook callback */
	arsdk_backend_net_socket_cb(backend_net->parent, fd, kind);
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
static void parse_proto_versions(json_object *object,
		uint32_t *v_min, uint32_t *v_max)
{
	/* by default only the protocol version 1 is considered as supported */
	uint32_t proto_v_min = 1;
	uint32_t proto_v_max = 1;
	json_object *jobj = NULL;

	if (!object)
		goto out;

	jobj = get_json_object(object, ARSDK_CONN_JSON_KEY_PROTO_V_MIN);
	if (jobj != NULL)
		proto_v_min = json_object_get_int(jobj);

	jobj = get_json_object(object, ARSDK_CONN_JSON_KEY_PROTO_V_MAX);
	if (jobj != NULL)
		proto_v_max = json_object_get_int(jobj);

out:
	*v_min = proto_v_min;
	*v_max = proto_v_max;
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
static int peer_conn_req_parse(
		struct arsdk_req *req,
		const void *data,
		size_t len)
{
	json_object *jroot = NULL;
	json_object *jctrl_name = NULL;
	json_object *jctrl_type = NULL;
	json_object *jdevice_id = NULL;
	const char *svalue = NULL;

	if (req == NULL || data == NULL || len == 0)
		return -EINVAL;

	/* Copy full json (and make it null terminated) */
	req->json = calloc(1, len + 1);
	if (req->json == NULL)
		return -ENOMEM;
	memcpy(req->json, data, len);
	req->json[len] = '\0';

	ARSDK_LOGI("Received json:");
	ARSDK_LOGI_STR(req->json);

	/* Parse json request */
	jroot = json_tokener_parse(req->json);
	if (jroot == NULL)
		goto error;

	/* Parse data port number */
	if (parse_port(jroot, ARSDK_CONN_JSON_KEY_D2CPORT,
			&req->d2c_data_port) < 0) {
		goto error;
	}

	/* Parse controller name */
	jctrl_name = get_json_object(jroot,
			ARSDK_CONN_JSON_KEY_CONTROLLER_NAME);
	if (jctrl_name != NULL) {
		svalue = json_object_get_string(jctrl_name);
		if (svalue != NULL)
			req->ctrl_name = xstrdup(svalue);
	}

	/* Parse controller type */
	jctrl_type = get_json_object(jroot,
			ARSDK_CONN_JSON_KEY_CONTROLLER_TYPE);
	if (jctrl_type != NULL) {
		svalue = json_object_get_string(jctrl_type);
		if (svalue != NULL)
			req->ctrl_type = xstrdup(svalue);
	}

	/* Parse requested device id */
	jdevice_id = get_json_object(jroot,
			ARSDK_CONN_JSON_KEY_DEVICE_ID);
	if (jdevice_id != NULL) {
		svalue = json_object_get_string(jdevice_id);
		if (svalue != NULL)
			req->device_id = xstrdup(svalue);
	}

	/* Parse requested QOS mode:
	 * if not present QOS is unsupported by the peer */
	req->qos_mode = parse_qos_mode(jroot);

	/* Parse supported protocol versions:
	 * by default only the protocol version 1 is considered as supported */
	parse_proto_versions(jroot, &req->proto_v_min, &req->proto_v_max);

	/* Success */
	json_object_put(jroot);
	return 0;

	/* Cleanup in case of error */
error:
	ARSDK_LOGE("Failed to parse json request: '%s'", req->json);
	if (jroot != NULL)
		json_object_put(jroot);
	return -EINVAL;
}

/**
 */
static void peer_conn_req_clear(struct arsdk_req *req)
{
	free(req->ctrl_name);
	free(req->ctrl_type);
	free(req->device_id);
	free(req->json);
}

/**
 */
static int peer_conn_setup_transport(struct arsdk_peer_conn *self,
		struct arsdk_backend_net *backend_net)
{
	int res = 0;
	struct arsdk_transport_net_cfg cfg;
	struct arsdk_transport_net_cbs transport_net_cbs;

	memset(&cfg, 0, sizeof(cfg));
	cfg.data.rx_port = ARSDK_NET_DEFAULT_C2D_DATA_PORT;
	cfg.stream_supported = backend_net->stream_supported;
	cfg.proto_v = self->proto_v;

	/* Create transport */
	memset(&transport_net_cbs, 0, sizeof(transport_net_cbs));
	transport_net_cbs.userdata = backend_net;
	transport_net_cbs.socketcb = &backend_net_socket_cb;
	res = arsdk_transport_net_new(self->loop, &cfg,
			&transport_net_cbs, &self->transport);
	if (res < 0)
		goto error;

	/* Retrieve bound ports */
	res = arsdk_transport_net_get_cfg(self->transport, &cfg);
	if (res < 0)
		goto error;

	/* Success */
	self->c2d_data_port = cfg.data.rx_port;
	return 0;

	/* Cleanup in case of error */
error:
	if (self->transport != NULL) {
		arsdk_transport_destroy(arsdk_transport_net_get_parent(
				self->transport));
		self->transport = NULL;
	};
	return res;
}

/**
 */
static int peer_conn_send_json(struct arsdk_peer_conn *self,
		int status,
		const char *json)
{
	int res = 0;
	json_object *jroot = NULL;
	const char *newjson = NULL;
	struct pomp_buffer *buf = NULL;

	/* Parse given json */
	if (json != NULL)
		jroot = json_tokener_parse(json);
	else
		jroot = json_object_new_object();
	if (jroot == NULL) {
		res = -EINVAL;
		goto out;
	}

	/* Add status and data port */
	json_object_object_add(jroot, ARSDK_CONN_JSON_KEY_STATUS,
			json_object_new_int(status));
	json_object_object_add(jroot, ARSDK_CONN_JSON_KEY_C2DPORT,
			json_object_new_int(self->c2d_data_port));

	/* Add chosen QOS mode */
	json_object_object_add(jroot, ARSDK_CONN_JSON_KEY_QOS_MODE,
			       json_object_new_int(self->qos_mode));

	/* Add protocol version to use */
	json_object_object_add(jroot, ARSDK_CONN_JSON_KEY_PROTO_V,
			json_object_new_int(self->proto_v));

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
	if (buf == NULL) {
		res = -ENOMEM;
		goto out;
	}

	/* Send buffer */
	res = pomp_conn_send_raw_buf(self->conn, buf);

out:
	if (jroot != NULL)
		json_object_put(jroot);
	if (buf != NULL)
		pomp_buffer_unref(buf);
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
		res = arsdk_transport_stop(arsdk_transport_net_get_parent(
				self->transport));
		if (res < 0)
			ARSDK_LOG_ERRNO("arsdk_transport_stop", -res);
		res = arsdk_transport_destroy(arsdk_transport_net_get_parent(
				self->transport));
		if (res < 0)
			ARSDK_LOG_ERRNO("arsdk_transport_destroy", -res);
	}

	free(self);
	return 0;
}

/**
 */
static int peer_conn_new(struct arsdk_backend_net *backend,
		struct pomp_conn *conn,
		uint32_t proto_v_min,
		uint32_t proto_v_max,
		int qos_mode_supported,
		int stream_supported,
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
	self->loop = backend->loop;
	self->conn = conn;
	self->state = PEER_CONN_STATE_PENDING;
	self->proto_v_min = proto_v_min;
	self->proto_v_max = proto_v_max;
	self->qos_mode_supported = qos_mode_supported;
	self->stream_supported = stream_supported;
	*ret_conn = self;
	return 0;
}

/**
 */
static void backend_net_event_cb(struct pomp_ctx *ctx,
		enum pomp_event event,
		struct pomp_conn *conn,
		const struct pomp_msg *msg,
		void *userdata)
{
	struct arsdk_backend_net *self = userdata;
	const struct sockaddr *addr1 = NULL;
	const struct sockaddr *addr2 = NULL;
	uint32_t addrlen1 = 0;
	uint32_t addrlen2 = 0;
	char addrbuf1[64] = "";
	char addrbuf2[64] = "";

	switch (event) {
	case POMP_EVENT_CONNECTED:
		/* Only one pending connection request at a time */
		if (self->listen.conn != NULL) {
			addr1 = pomp_conn_get_peer_addr(
					self->listen.conn->conn, &addrlen1);
			addr2 = pomp_conn_get_peer_addr(
					conn, &addrlen2);
			pomp_addr_format(addrbuf1, sizeof(addrbuf1),
					addr1, addrlen1);
			pomp_addr_format(addrbuf2, sizeof(addrbuf2),
					addr2, addrlen2);

			ARSDK_LOGI("Abort current json connection request "
					"from %s to handle new one from %s",
					addrbuf1, addrbuf2);

			pomp_conn_disconnect(self->listen.conn->conn);
			peer_conn_destroy(self->listen.conn);
			self->listen.conn = NULL;
		}
		if (peer_conn_new(self, conn,
				self->proto_v_min, self->proto_v_max,
				self->qos_mode_supported,
				self->stream_supported,
				&self->listen.conn) < 0) {
			pomp_conn_disconnect(conn);
		}
		break;

	case POMP_EVENT_DISCONNECTED:
		/* Clear pending connection request */
		if (self->listen.conn != NULL
				&& self->listen.conn->conn == conn) {
			peer_conn_destroy(self->listen.conn);
			self->listen.conn = NULL;
		}
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
static void backend_net_raw_cb(struct pomp_ctx *ctx,
		struct pomp_conn *conn,
		struct pomp_buffer *buf,
		void *userdata)
{
	int res = 0;
	struct arsdk_backend_net *self = userdata;
	const struct sockaddr *peer_addr = NULL;
	uint32_t addrlen = 0;
	const void *cdata = NULL;
	size_t len = 0;
	struct arsdk_req req;
	struct arsdk_peer_info info;
	const struct arsdk_peer_info *pinfo = NULL;
	char ip[128] = "";
	int proto_v_min;
	int proto_v_max;

	memset(&req, 0, sizeof(req));
	memset(&info, 0, sizeof(info));

	/* Only one pending connection request at a time */
	if (self->listen.conn == NULL || self->listen.conn->conn != conn)
		return;

	/* Ignore data received if a peer has already been created */
	if (self->listen.conn->peer != NULL)
		return;

	/* Get peer address */
	peer_addr = pomp_conn_get_peer_addr(conn, &addrlen);
	if (peer_addr == NULL || addrlen != sizeof(struct sockaddr_in)
			|| peer_addr->sa_family != AF_INET) {
		ARSDK_LOGE("Bad connection request address");
		return;
	}
	self->listen.conn->in_addr = ntohl(((const struct sockaddr_in *)
			peer_addr)->sin_addr.s_addr);
	getnameinfo(peer_addr, addrlen, ip, sizeof(ip),
			NULL, 0, NI_NUMERICHOST);

	/* Get data from buffer */
	pomp_buffer_get_cdata(buf, &cdata, &len, NULL);

	/* Parse json request */
	res = peer_conn_req_parse(&req, cdata, len);
	if (res < 0)
		goto out;

	/* choose the real protocol version according to
	 * the protocol versions supported by the peer and the backend */
	proto_v_min = MAX(req.proto_v_min, self->listen.conn->proto_v_min);
	proto_v_max = MIN(req.proto_v_max, self->listen.conn->proto_v_max);
	if (proto_v_min > proto_v_max) {
		ARSDK_LOGW("peer protocol versions supported[%d:%d] "
			   "don't match with "
			   "backend protocol versions supported[%d:%d]",
			   req.proto_v_min,
			   req.proto_v_max,
			   self->listen.conn->proto_v_min,
			   self->listen.conn->proto_v_max);
		goto out;
	}
	self->listen.conn->proto_v = proto_v_max;

	/* choose the real qos_mode according to
	 * the qos_mode requested by the peer and the qos_mode supported */
	if (req.qos_mode != self->listen.conn->qos_mode_supported) {
		self->listen.conn->qos_mode = 0;
		ARSDK_LOGW("ip/mac: QOS requested(%d) != QOS supported(%d)",
				req.qos_mode,
				self->listen.conn->qos_mode_supported);
	} else {
		self->listen.conn->qos_mode = req.qos_mode;
	}

	/* Create peer */
	self->listen.conn->d2c_data_port = req.d2c_data_port;
	self->listen.conn->d2c_rtp_port = req.d2c_rtp_port;
	self->listen.conn->d2c_rtcp_port = req.d2c_rtcp_port;
	info.proto_v = proto_v_max;
	info.ctrl_name = req.ctrl_name;
	info.ctrl_type = req.ctrl_type;
	info.ctrl_addr = ip;
	info.device_id = req.device_id;
	info.json = req.json;

	/* create peer */
	res = arsdk_backend_create_peer(self->parent, &info, self->listen.conn,
			&self->listen.conn->peer);
	if (res < 0)
		goto out;
	res = arsdk_peer_get_info(self->listen.conn->peer, &pinfo);
	if (res < 0)
		goto out;

	/* Notify connection request */
	(*self->listen.cbs.conn_req)(self->listen.conn->peer, pinfo,
			self->listen.cbs.userdata);

out:
	/* TODO: cleanup */
	peer_conn_req_clear(&req);
	/*if (listen->pending.peer != NULL) {
		arsdk_peer_unref(listen->pending.peer);
		listen->pending.peer = NULL;
	}*/
}

/**
 */
static void backend_net_pomp_socket_cb(
		struct pomp_ctx *ctx,
		int fd,
		enum pomp_socket_kind kind,
		void *userdata)
{
	struct arsdk_backend_net *self = userdata;

	ARSDK_RETURN_IF_FAILED(self != NULL, -EINVAL);

	/* socket hook callback */
	arsdk_backend_net_socket_cb(self->parent, fd,
			ARSDK_SOCKET_KIND_CONNECTION);
}

/**
 */
int arsdk_backend_net_start_listen(struct arsdk_backend_net *self,
		const struct arsdk_backend_listen_cbs *cbs,
		uint16_t port)
{
	int res = 0;
	struct sockaddr_in addr;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(port != 0, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->conn_req != NULL, -EINVAL);

	if (self->listen.ctx != NULL)
		return -EBUSY;

	self->listen.cbs = *cbs;

	/* Create pomp context, make it raw */
	self->listen.ctx = pomp_ctx_new_with_loop(&backend_net_event_cb,
			self, self->loop);
	if (self->listen.ctx == NULL) {
		res = -ENOMEM;
		goto error;
	}

	res = pomp_ctx_set_raw(self->listen.ctx, &backend_net_raw_cb);
	if (res < 0)
		goto error;

	/* Disable TCP keepalive */
	res = pomp_ctx_setup_keepalive(self->listen.ctx, 0, 0, 0, 0);
	if (res < 0)
		goto error;

	/* Set socket hook callback */
	res = pomp_ctx_set_socket_cb(self->listen.ctx,
			&backend_net_pomp_socket_cb);
	if (res < 0) {
		ARSDK_LOG_ERRNO("pomp_ctx_set_socket_cb", -res);
		goto error;
	}

	/* Setup address to list on given port */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);
	res = pomp_ctx_listen(self->listen.ctx,
			(const struct sockaddr *)&addr,
			sizeof(addr));
	if (res < 0)
		goto error;

	return 0;

	/* TODO: cleanup in case of error */
error:
	return res;
}

/**
 */
int arsdk_backend_net_stop_listen(struct arsdk_backend_net *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	if (self->listen.ctx == NULL)
		return 0;

	/* Free pending peer connection request */
	if (self->listen.conn != NULL) {
		peer_conn_destroy(self->listen.conn);
		self->listen.conn = NULL;
	}

	/* Stop and destroy pomp context */
	pomp_ctx_stop(self->listen.ctx);
	pomp_ctx_destroy(self->listen.ctx);
	self->listen.ctx = NULL;
	return 0;
}

/**
 */
static int arsdk_backend_net_accept_peer_conn(
		struct arsdk_backend *base,
		struct arsdk_peer *peer,
		struct arsdk_peer_conn *conn,
		const struct arsdk_peer_conn_cfg *cfg,
		const struct arsdk_peer_conn_internal_cbs *cbs,
		struct pomp_loop *loop)
{
	int res = 0;
	struct arsdk_backend_net *self = arsdk_backend_get_child(base);
	struct arsdk_transport_net_cfg net_cfg;

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

	/* Setup transport (to get rx port) */
	res = peer_conn_setup_transport(conn, self);
	if (res < 0)
		goto error;

	/* Send json response */
	res = peer_conn_send_json(conn, 0, cfg->json);
	if (res < 0)
		goto error;

	/* Setup tx address and ports */
	memset(&net_cfg, 0, sizeof(net_cfg));
	res = arsdk_transport_net_get_cfg(conn->transport, &net_cfg);
	if (res < 0)
		goto error;
	net_cfg.tx_addr = conn->in_addr;
	net_cfg.qos_mode = conn->qos_mode;
	net_cfg.data.tx_port = conn->d2c_data_port;
	res = arsdk_transport_net_update_cfg(conn->transport, &net_cfg);
	if (res < 0)
		goto error;

	/* We don't need the connection anymore */
	self->listen.conn = NULL;

	/* Notify connection */
	conn->state = PEER_CONN_STATE_CONNECTED;
	(*conn->cbs.connected)(peer, conn,
			arsdk_transport_net_get_parent(conn->transport),
			conn->cbs.userdata);

	/* Success */
	return 0;

	/* TODO: Cleanup in case of error */
error:
	return res;
}

/**
 */
static int arsdk_backend_net_reject_peer_conn(struct arsdk_backend *base,
		struct arsdk_peer *peer,
		struct arsdk_peer_conn *conn)
{
	struct arsdk_backend_net *self = arsdk_backend_get_child(base);

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(peer != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(conn != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(conn->peer == peer, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(self->listen.conn == conn, -EINVAL);

	/* Send json negative response */
	if (conn->conn != NULL)
		peer_conn_send_json(conn, -1, NULL);

	/* Cleanup connection */
	peer_conn_destroy(conn);
	self->listen.conn = NULL;
	return 0;
}

/**
 */
static int arsdk_backend_net_stop_peer_conn(struct arsdk_backend *base,
		struct arsdk_peer *peer,
		struct arsdk_peer_conn *conn)
{
	struct arsdk_backend_net *self = arsdk_backend_get_child(base);

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(peer != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(conn != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(conn->peer == peer, -EINVAL);

	/* If this is the pending peer, it is actually a reject */
	if (self->listen.conn == conn) {
		ARSDK_LOGW("peer %p: reject instead of disconnect", peer);
		return arsdk_backend_net_reject_peer_conn(base, peer, conn);
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

/**
 */
static const struct arsdk_backend_ops s_arsdk_backend_net_ops = {
	.accept_peer_conn = &arsdk_backend_net_accept_peer_conn,
	.reject_peer_conn = &arsdk_backend_net_reject_peer_conn,
	.stop_peer_conn = &arsdk_backend_net_stop_peer_conn,
	.socket_cb = &arsdk_backend_net_socket_cb,
};

/**
 */
int arsdk_backend_net_new(struct arsdk_mngr *mngr,
		const struct arsdk_backend_net_cfg *cfg,
		struct arsdk_backend_net **ret_obj)
{
	int res = 0;
	struct arsdk_backend_net *self = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(cfg != NULL, -EINVAL);
#if ARSDK_BACKEND_NET_PROTO_MIN > 1
	ARSDK_RETURN_ERR_IF_FAILED(cfg->proto_v_min == 0 ||
			cfg->proto_v_min >= ARSDK_BACKEND_NET_PROTO_MIN,
			-EINVAL);
#endif
	ARSDK_RETURN_ERR_IF_FAILED(cfg->proto_v_max == 0 ||
			cfg->proto_v_max <= ARSDK_BACKEND_NET_PROTO_MAX,
			-EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg->proto_v_min <= cfg->proto_v_max,
			-EINVAL);

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Setup base structure */
	res = arsdk_backend_new(self, mngr, "net", ARSDK_BACKEND_TYPE_NET,
			&s_arsdk_backend_net_ops, &self->parent);
	if (res < 0)
		goto error;

	/* Initialize structure */
	self->loop = arsdk_mngr_get_loop(mngr);
	self->iface = xstrdup(cfg->iface);
	self->qos_mode_supported = cfg->qos_mode_supported;
	self->stream_supported = cfg->stream_supported;
	self->proto_v_min = cfg->proto_v_min;
	self->proto_v_max = cfg->proto_v_max;
	/* by default all protocol versions implemented are supported */
	self->proto_v_min = cfg->proto_v_min != 0 ? self->proto_v_min :
			ARSDK_BACKEND_NET_PROTO_MIN;
	self->proto_v_max = cfg->proto_v_max != 0 ? self->proto_v_max :
			ARSDK_BACKEND_NET_PROTO_MAX;

	/* Success */
	*ret_obj = self;
	return 0;

	/* TODO: Cleanup in case of error */
error:
	return res;
}

/**
 */
int arsdk_backend_net_destroy(struct arsdk_backend_net *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	/* destroy backend */
	arsdk_backend_destroy(self->parent);

	/* Free resources */
	free(self->iface);
	free(self);
	return 0;
}

int arsdk_backend_net_set_socket_cb(struct arsdk_backend_net *self,
		arsdk_backend_net_socket_cb_t cb, void *userdata)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	self->socketcb = cb;
	self->userdata = userdata;

	return 0;
}


/**
 */
struct arsdk_backend *arsdk_backend_net_get_parent(
		struct arsdk_backend_net *self)
{
	return self ? self->parent : NULL;
}
