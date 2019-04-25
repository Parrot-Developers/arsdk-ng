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
#include "arsdk_ftp_log.h"
#include "arsdk_ftp_cmd.h"
#include "arsdk_ftp_conn.h"

enum arsdk_ftp_conn_state {
	/* disconnected */
	ARSDK_FTP_CONN_STATE_DISCONNECTED = 0,
	/* waiting for tcp connection */
	ARSDK_FTP_CONN_STATE_TCPCONNECTING,
	/* waiting for ftp connection (220) */
	ARSDK_FTP_CONN_STATE_FTPCONNECTING,
	/* waiting for user response (230) */
	ARSDK_FTP_CONN_STATE_LOGIN_USER,
	/* waiting for pass response (230) */
	ARSDK_FTP_CONN_STATE_LOGIN_PASS,
	/* connected */
	ARSDK_FTP_CONN_STATE_CONNECTED,
};

enum arsdk_ftp_conn_event {
	/* tcp disconnect (no ftp code) */
	ARSDK_FTP_CONN_EVENT_TCPDISCONNECT,
	/* tcp connect (no ftp code) */
	ARSDK_FTP_CONN_EVENT_TCPCONNECT,
	/* ftp response code received */
	ARSDK_FTP_CONN_EVENT_FTP_RECV,
	/* failed to parse ftp response (no ftp code)*/
	ARSDK_FTP_CONN_EVENT_FTP_RECV_FAILED,
};

#define ARSDK_FTP_DEFAULT_USER "anonymous"
#define ARSDK_FTP_DEFAULT_PASS ""

struct arsdk_ftp_conn_listener {
	struct arsdk_ftp_conn_cbs       cbs;
	struct list_node                node;
};

struct arsdk_ftp_conn {
	struct pomp_loop                *loop;
	struct pomp_ctx                 *ctx;
	enum arsdk_ftp_conn_state       state;
	char                            *username;
	char                            *password;
	struct list_node                listeners;
};

static int arsdk_ftp_conn_listener_new(const struct arsdk_ftp_conn_cbs *cbs,
		struct arsdk_ftp_conn_listener **ret_listener)
{
	struct arsdk_ftp_conn_listener *listener = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_listener != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->connected != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->disconnected != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->recv_response != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->socketcb != NULL, -EINVAL);

	listener = calloc(1, sizeof(*listener));
	if (listener == NULL)
		return -ENOMEM;

	listener->cbs = *cbs;

	*ret_listener = listener;
	return 0;
}

static void arsdk_ftp_conn_listener_destroy(
		struct arsdk_ftp_conn_listener *listener)
{
	free(listener);
}

int arsdk_ftp_conn_send(struct arsdk_ftp_conn *conn, struct pomp_buffer *buff)
{
	int res = 0;
	size_t len = 0;
	const void *cdata;

	ARSDK_RETURN_ERR_IF_FAILED(conn != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(buff != NULL, -EINVAL);

	/* log request */
	res = pomp_buffer_get_cdata(buff, &cdata, &len, NULL);
	if (res < 0)
		return res;
	ARSDK_LOGI("> %.*s", (int)(len - 2), (const char *)cdata);

	/* send request */
	res = pomp_ctx_send_raw_buf(conn->ctx, buff);
	return res;
}

static void ftp_connected(struct arsdk_ftp_conn *conn)
{
	struct arsdk_ftp_conn_listener *listener = NULL;
	struct arsdk_ftp_conn_listener *listener_tmp = NULL;

	ARSDK_RETURN_IF_FAILED(conn != NULL, -EINVAL);

	conn->state = ARSDK_FTP_CONN_STATE_CONNECTED;

	/* connected callback */
	list_walk_entry_forward_safe(
			&conn->listeners, listener, listener_tmp, node) {
		(*listener->cbs.connected)(conn, listener->cbs.userdata);
	}
}

static void ftp_disconnected(struct arsdk_ftp_conn *conn)
{
	struct arsdk_ftp_conn_listener *listener = NULL;
	struct arsdk_ftp_conn_listener *listener_tmp = NULL;

	ARSDK_RETURN_IF_FAILED(conn != NULL, -EINVAL);

	conn->state = ARSDK_FTP_CONN_STATE_DISCONNECTED;

	/* disconnected callback */
	list_walk_entry_forward_safe(
			&conn->listeners, listener, listener_tmp, node) {
		(*listener->cbs.disconnected)(conn, listener->cbs.userdata);
	}
}

static int send_user(struct arsdk_ftp_conn *conn)
{
	int res = 0;
	struct pomp_buffer *buff = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(conn != NULL, -EINVAL);

	res = arsdk_ftp_cmd_enc_user(conn->username, &buff);
	if (res < 0)
		return res;

	res = arsdk_ftp_conn_send(conn, buff);
	if (res < 0)
		goto end;

	conn->state = ARSDK_FTP_CONN_STATE_LOGIN_USER;
end:
	pomp_buffer_unref(buff);
	return res;
}

static int send_pass(struct arsdk_ftp_conn *conn)
{
	int res = 0;
	struct pomp_buffer *buff = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(conn != NULL, -EINVAL);

	res = arsdk_ftp_cmd_enc_pass(conn->password, &buff);
	if (res < 0)
		return res;

	res = arsdk_ftp_conn_send(conn, buff);
	if (res < 0)
		goto end;

	conn->state = ARSDK_FTP_CONN_STATE_LOGIN_PASS;

end:
	pomp_buffer_unref(buff);
	return res;
}

static int ftp_received(struct arsdk_ftp_conn *conn,
		struct arsdk_ftp_cmd_result *response)
{
	struct arsdk_ftp_conn_listener *listener = NULL;
	struct arsdk_ftp_conn_listener *listener_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(conn != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(response != NULL, -EINVAL);

	switch (conn->state) {
	case ARSDK_FTP_CONN_STATE_FTPCONNECTING:
		/* waiting for ftp connection (220) */
		if (response->code != 220)
			return -EPERM;

		return send_user(conn);
	case ARSDK_FTP_CONN_STATE_LOGIN_USER:
		/* waiting for user response (230) */
		if (response->code != 230)
			return -EPERM;

		return send_pass(conn);
	case ARSDK_FTP_CONN_STATE_LOGIN_PASS:
		/* waiting for pass response (230) */
		if (response->code != 230)
			return -EPERM;

		ftp_connected(conn);
		break;

	case ARSDK_FTP_CONN_STATE_CONNECTED:
		/* response received callback */
		list_walk_entry_forward_safe(&conn->listeners,
				listener, listener_tmp, node) {
			(*listener->cbs.recv_response)(conn, response,
					listener->cbs.userdata);
		}
		break;
	case ARSDK_FTP_CONN_STATE_DISCONNECTED:
	case ARSDK_FTP_CONN_STATE_TCPCONNECTING:
	default:
		return -EPERM;
		break;
	}

	return 0;
}

static void process_event(struct arsdk_ftp_conn *conn,
		enum arsdk_ftp_conn_event event,
		struct arsdk_ftp_cmd_result *response)
{
	int res = 0;

	ARSDK_RETURN_IF_FAILED(conn != NULL, -EINVAL);

	switch (event) {
	case ARSDK_FTP_CONN_EVENT_TCPDISCONNECT:
		if (conn->state == ARSDK_FTP_CONN_STATE_DISCONNECTED)
			goto error;

		ftp_disconnected(conn);
		break;
	case ARSDK_FTP_CONN_EVENT_TCPCONNECT:
		if (conn->state != ARSDK_FTP_CONN_STATE_TCPCONNECTING)
			goto error;

		/* tcp connected, wait for ftp 220 response code */
		conn->state = ARSDK_FTP_CONN_STATE_FTPCONNECTING;
		break;
	case ARSDK_FTP_CONN_EVENT_FTP_RECV:
		res = ftp_received(conn, response);
		if (res < 0)
			goto error;

		break;
	case ARSDK_FTP_CONN_EVENT_FTP_RECV_FAILED:
	default:
		goto error;
		break;
	}

	return;

error:
	ARSDK_LOGE("event not expected ; "
			"event %d ftp code: %d ; current state: %d",
			event,
			(response != NULL) ? response->code : 0,
			conn->state);

	if (conn->state != ARSDK_FTP_CONN_STATE_DISCONNECTED)
		ftp_disconnected(conn);
}

static void arsdk_ftp_conn_event_cb(struct pomp_ctx *ctx,
		enum pomp_event event, struct pomp_conn *sk_conn,
		const struct pomp_msg *msg, void *userdata)
{
	struct arsdk_ftp_conn *conn = userdata;

	ARSDK_RETURN_IF_FAILED(conn != NULL, -EINVAL);

	switch (event) {
	case POMP_EVENT_CONNECTED:
		/* tcp connected */
		process_event(conn, ARSDK_FTP_CONN_EVENT_TCPCONNECT, NULL);
		break;
	case POMP_EVENT_DISCONNECTED:
		/* tcp disconnected */
		process_event(conn, ARSDK_FTP_CONN_EVENT_TCPDISCONNECT, NULL);
		break;
	default:
		break;
	}
}

static void arsdk_ftp_conn_recv_cb(struct pomp_ctx *ctx,
		struct pomp_conn *sk_conn,
		struct pomp_buffer *buff, void *userdata)
{
	struct arsdk_ftp_conn *conn = userdata;
	struct arsdk_ftp_cmd_result response;
	int res = 0;

	ARSDK_RETURN_IF_FAILED(conn != NULL, -EINVAL);

	res = arsdk_ftp_cmd_dec(buff, &response);
	if (res < 0) {
		ARSDK_LOGE("Fail to parse ftp response");
		process_event(conn, ARSDK_FTP_CONN_EVENT_FTP_RECV_FAILED, NULL);
	}

	process_event(conn, ARSDK_FTP_CONN_EVENT_FTP_RECV, &response);
	return;
}

static int arsdk_ftp_conn_clear_listeners(struct arsdk_ftp_conn *conn)
{
	struct arsdk_ftp_conn_listener *listener = NULL;
	struct arsdk_ftp_conn_listener *listener_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(conn != NULL, -EINVAL);

	/* check if listener already exist */
	list_walk_entry_forward_safe(
			&conn->listeners, listener, listener_tmp, node) {
		list_del(&listener->node);
		arsdk_ftp_conn_listener_destroy(listener);
	}

	return -ENXIO;
}

static void socket_cb(struct pomp_ctx *ctx, int fd, enum pomp_socket_kind kind,
		void *userdata)
{
	struct arsdk_ftp_conn *conn = userdata;
	struct arsdk_ftp_conn_listener *listener = NULL;
	struct arsdk_ftp_conn_listener *listener_tmp = NULL;

	ARSDK_RETURN_IF_FAILED(conn != NULL, -EINVAL);

	/* socket hook callback */
	list_walk_entry_forward_safe(
			&conn->listeners, listener, listener_tmp, node) {
		(*listener->cbs.socketcb)(conn, fd, listener->cbs.userdata);
	}

}

int arsdk_ftp_conn_new(struct pomp_loop *loop,
		const struct sockaddr *addr,
		uint32_t addrlen,
		const char *username,
		const char *password,
		struct arsdk_ftp_conn **ret_conn)
{
	struct arsdk_ftp_conn *conn;
	int res = 0;

	ARSDK_RETURN_ERR_IF_FAILED(loop != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(addr != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_conn != NULL, -EINVAL);

	conn = calloc(1, sizeof(*conn));
	if (conn == NULL)
		return -ENOMEM;

	conn->loop = loop;
	conn->state = ARSDK_FTP_CONN_STATE_TCPCONNECTING;
	conn->username = (username != NULL) ? xstrdup(username) :
				xstrdup(ARSDK_FTP_DEFAULT_USER);
	conn->password = (password != NULL) ? xstrdup(password) :
			xstrdup(ARSDK_FTP_DEFAULT_PASS);
	list_init(&conn->listeners);

	/* create socket context */
	conn->ctx = pomp_ctx_new_with_loop(&arsdk_ftp_conn_event_cb, conn,
			conn->loop);
	if (conn->ctx == NULL) {
		res = -ENOMEM;
		goto error;
	}

	res = pomp_ctx_set_socket_cb(conn->ctx, &socket_cb);
	if (res < 0)
		goto error;

	/* set raw and disable keepalive */
	pomp_ctx_set_raw(conn->ctx, &arsdk_ftp_conn_recv_cb);
	pomp_ctx_setup_keepalive(conn->ctx, 0, 0, 0, 0);

	/* try connection */
	res = pomp_ctx_connect(conn->ctx, addr, addrlen);
	if (res < 0)
		goto error;

	*ret_conn = conn;
	return 0;
error:
	arsdk_ftp_conn_destroy(conn);
	return res;
}

int arsdk_ftp_conn_destroy(struct arsdk_ftp_conn *conn)
{
	ARSDK_RETURN_ERR_IF_FAILED(conn != NULL, -EINVAL);

	/* remove all listeners */
	arsdk_ftp_conn_clear_listeners(conn);

	pomp_ctx_stop(conn->ctx);
	pomp_ctx_destroy(conn->ctx);

	free(conn->password);
	free(conn->username);
	free(conn);
	return 0;
}

int arsdk_ftp_conn_is_connected(struct arsdk_ftp_conn *conn)
{
	if ((conn == NULL) ||
	    (conn->state != ARSDK_FTP_CONN_STATE_CONNECTED))
		return 0;
	else
		return 1;
}

const struct sockaddr *arsdk_ftp_conn_get_addr(struct arsdk_ftp_conn *conn,
		uint32_t *addrlen)
{
	struct pomp_conn *pomp_conn = NULL;

	if (conn == NULL)
		return NULL;

	pomp_conn = pomp_ctx_get_conn(conn->ctx);
	if (pomp_conn == NULL)
		return NULL;

	return pomp_conn_get_peer_addr(pomp_conn, addrlen);
}

int arsdk_ftp_conn_add_listener(struct arsdk_ftp_conn *conn,
		const struct arsdk_ftp_conn_cbs *cbs)
{
	int res = 0;
	struct arsdk_ftp_conn_listener *listener_tmp = NULL;
	struct arsdk_ftp_conn_listener *new_listener = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(conn != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->userdata != NULL, -EINVAL);

	/* check if listener already exist */
	list_walk_entry_forward(&conn->listeners, listener_tmp, node) {
		if (listener_tmp->cbs.userdata == cbs->userdata)
			return -EBUSY;
	}

	res = arsdk_ftp_conn_listener_new(cbs, &new_listener);
	if (res < 0)
		return res;

	list_add_before(&conn->listeners, &new_listener->node);
	return 0;
}

int arsdk_ftp_conn_remove_listener(struct arsdk_ftp_conn *conn,
		const void *userdata)
{
	struct arsdk_ftp_conn_listener *listener = NULL;
	struct arsdk_ftp_conn_listener *listener_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(conn != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(userdata != NULL, -EINVAL);

	/* check if listener already exist */
	list_walk_entry_forward_safe(
			&conn->listeners, listener, listener_tmp, node) {
		if (listener->cbs.userdata == userdata) {
			list_del(&listener->node);
			arsdk_ftp_conn_listener_destroy(listener);
			return 0;
		}
	}

	return -ENXIO;
}
