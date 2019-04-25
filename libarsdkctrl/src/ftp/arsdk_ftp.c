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
#include "arsdk_ftp_seq.h"
#include "arsdk_ftp.h"

#ifdef BUILD_LIBULOG
ULOG_DECLARE_TAG(arsdk_ftp);
#endif /* BUILD_LIBULOG */

enum arsdk_ftp_req_type {
	ARSDK_FTP_REQ_TYPE_GET = 0,
	ARSDK_FTP_REQ_TYPE_PUT,
	ARSDK_FTP_REQ_TYPE_RENAME,
	ARSDK_FTP_REQ_TYPE_LIST,
	ARSDK_FTP_REQ_TYPE_DELETE,
	ARSDK_FTP_REQ_TYPE_SIZE,
	ARSDK_FTP_REQ_TYPE_COUNT,
};

struct arsdk_ftp_req {
	struct arsdk_ftp                *ctx;
	struct arsdk_ftp_req_cbs        cbs;
	struct list_node                node;
	enum arsdk_ftp_req_type         type;
	char                            *url;
	struct arsdk_ftp_conn_elem      *conn_elem;
	struct arsdk_ftp_seq            *ftp_seq;
	struct {
		size_t                  tsize;
		size_t                  size;
	} stream;
	uint8_t                         is_aborted;
};

struct arsdk_ftp {
	/* event loop */
	struct pomp_loop *loop;

	/* ftp connection credentials */
	char *username;
	char *password;

	/* idle connections */
	struct list_node conns_idle;
	/* busy connections */
	struct list_node conns_busy;

	/* request list */
	struct list_node requests;

	struct arsdk_ftp_cbs cbs;
};

enum arsdk_ftp_conn_elem_state {
	ARSDK_FTP_CONN_ELEM_STATE_CONNECTING,
	ARSDK_FTP_CONN_ELEM_STATE_READY,
	ARSDK_FTP_CONN_ELEM_STATE_USED,
};

struct arsdk_ftp_conn_elem {
	struct arsdk_ftp_conn           *conn;
	struct arsdk_ftp                *ctx;
	struct list_node                node;
};

int arsdk_ftp_new(struct pomp_loop *loop,
		const char *username,
		const char *password,
		const struct arsdk_ftp_cbs *cbs,
		struct arsdk_ftp **ret_ctx)
{
	struct arsdk_ftp *ctx;

	ARSDK_RETURN_ERR_IF_FAILED(loop != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->socketcb != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_ctx != NULL, -EINVAL);

	ctx = calloc(1, sizeof(*ctx));
	if (ctx == NULL)
		return -ENOMEM;

	ctx->loop = loop;
	ctx->username = xstrdup(username);
	ctx->password = xstrdup(password);
	ctx->cbs = *cbs;

	/* init list of idle connections */
	list_init(&ctx->conns_idle);
	/* init list of busy connections */
	list_init(&ctx->conns_busy);

	/* init list of pending requests */
	list_init(&ctx->requests);
	*ret_ctx = ctx;
	return 0;
}

int arsdk_ftp_destroy(struct arsdk_ftp *ctx)
{
	if (ctx == NULL)
		return -EINVAL;

	arsdk_ftp_stop(ctx);
	free(ctx->username);
	free(ctx->password);
	free(ctx);
	return 0;
}

/**
 */
static void conn_elem_destroy(struct arsdk_ftp_conn_elem *elem)
{
	ARSDK_RETURN_IF_FAILED(elem != NULL, -EINVAL);

	arsdk_ftp_conn_destroy(elem->conn);
	free(elem);
}

/**
 */
static int conn_elem_new(struct arsdk_ftp *ctx,
		struct sockaddr *addr, size_t addrlen,
		struct arsdk_ftp_conn_elem **ret_elem)
{
	int res = 0;
	struct arsdk_ftp_conn_elem *elem = NULL;

	elem = calloc(1, sizeof(*elem));
	if (elem == NULL)
		return -ENOMEM;

	/* Create connection */
	res = arsdk_ftp_conn_new(ctx->loop, addr, addrlen,
			ctx->username, ctx->password, &elem->conn);
	if (res < 0)
		goto error;

	elem->ctx = ctx;
	*ret_elem = elem;
	return 0;
error:
	conn_elem_destroy(elem);
	return res;
}


static void conn_elem_destroy_cb(void *userdata)
{
	conn_elem_destroy(userdata);
}

static void connected_cb(struct arsdk_ftp_conn *conn, void *userdata)
{
	/* do nothing */
}

static void disconnected_cb(struct arsdk_ftp_conn *conn, void *userdata)
{
	struct arsdk_ftp_conn_elem *elem = userdata;

	ARSDK_RETURN_IF_FAILED(elem != NULL, -EINVAL);

	/* delete connection */
	list_del(&elem->node);
	/* dispatch req destroy out of ftp connection ctx */
	pomp_loop_idle_add(elem->ctx->loop, &conn_elem_destroy_cb, elem);
}

static void recv_response_cb(struct arsdk_ftp_conn *conn,
		struct arsdk_ftp_cmd_result *response, void *userdata)
{
	/* do nothing */
}

static void conn_socket_cb(struct arsdk_ftp_conn *conn, int fd, void *userdata)
{
	struct arsdk_ftp_conn_elem *elem = userdata;

	/* socket hook callback */
	(*elem->ctx->cbs.socketcb)(elem->ctx, fd, ARSDK_SOCKET_KIND_FTP,
			elem->ctx->cbs.userdata);
}

static int url_to_addr(const char *url_src, struct sockaddr_in *addr)
{
	int res = 0;
	int port;
	char ip[17];

	res = sscanf(url_src, "ftp://%16[^:]:%u/", ip, &port);
	if (res < 2)
		return -EINVAL;

	/* Setup address */
	addr->sin_family = AF_INET;
	addr->sin_addr.s_addr = inet_addr(ip);
	addr->sin_port = htons(port);

	return 0;
}

static enum arsdk_ftp_status
seq_status_to_ftp_status(enum arsdk_ftp_seq_status status)
{
	switch (status) {
	case ARSDK_FTP_SEQ_STATUS_OK:
		return ARSDK_FTP_STATUS_OK;
	case ARSDK_FTP_SEQ_CANCELED:
		return ARSDK_FTP_STATUS_CANCELED;

	case ARSDK_FTP_SEQ_ABORTED:
		return ARSDK_FTP_STATUS_ABORTED;
	case ARSDK_FTP_SEQ_FAILED:
	default:
		return ARSDK_FTP_STATUS_FAILED;
	}
}

/**
 */
static void req_destroy(struct arsdk_ftp_req *req)
{
	struct arsdk_ftp_conn_elem *elem = NULL;
	struct arsdk_ftp_conn_elem *tmp = NULL;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(req->ctx != NULL, -EINVAL);

	list_walk_entry_forward_safe(&req->ctx->conns_busy, elem, tmp, node) {
		if (elem == req->conn_elem) {
			list_del(&elem->node);
			list_add_before(&req->ctx->conns_idle, &elem->node);
		}
	}

	arsdk_ftp_seq_destroy(req->ftp_seq);
	free(req->url);
	free(req);
}

static void seq_complete_cb(struct arsdk_ftp_seq *seq,
		enum arsdk_ftp_seq_status seq_status,
		int error,
		void *userdata)
{
	struct arsdk_ftp_req *req = userdata;
	enum arsdk_ftp_status status = ARSDK_FTP_STATUS_OK;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(req->ctx != NULL, -EINVAL);

	/* set status */
	status = seq_status_to_ftp_status(seq_status);
	if ((status == ARSDK_FTP_STATUS_OK) &&
	    (req->stream.tsize > 0) &&
	    (req->stream.tsize != req->stream.size))
		status = ARSDK_FTP_STATUS_FAILED;
	if (req->is_aborted)
		status = ARSDK_FTP_STATUS_ABORTED;

	(*req->cbs.complete)(req->ctx, req, status, error, req->cbs.userdata);

	/* cleanup */
	list_del(&req->node);
	req_destroy(userdata);
}

static int seq_data_recv_cb(struct arsdk_ftp_seq *seq,
		struct pomp_buffer *buff,
		void *userdata)
{
	struct arsdk_ftp_req *req = userdata;
	const void *cdata = NULL;
	size_t len = 0;
	int res = 0;
	size_t wr_len = 0;
	double dltotal = 0.0;
	double dlnow = 0.0;
	float dlpercent = 0.0f;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	res = pomp_buffer_get_cdata(buff, &cdata, &len, NULL);
	if (res < 0)
		return res;

	/* update stream info */
	req->stream.size += len;
	dltotal = req->stream.tsize;
	dlnow = req->stream.size;
	dlpercent = (dltotal == 0) ? 0.0f : (float)((dlnow / dltotal) * 100);

	/* progress callback */
	(*req->cbs.progress)(req->ctx, req,
			req->stream.tsize, req->stream.size, dlpercent,
			0, 0, 0, req->cbs.userdata);

	/* write callback */
	wr_len = (*req->cbs.write_data)(req->ctx, req, cdata, 1, len,
			req->cbs.userdata);
	if (wr_len != len)
		return -EIO;

	return 0;
}

static size_t seq_data_send_cb(struct arsdk_ftp_seq *seq, void *buffer,
		size_t cap, void *userdata)
{
	struct arsdk_ftp_req *req = userdata;
	size_t read_len = 0;
	double ultotal = 0.0;
	double ulnow = 0.0;
	float ulpercent = 0.0f;

	if (req == NULL)
		return 0;

	/* read callback */
	read_len = (*req->cbs.read_data)(req->ctx, req, buffer, 1, cap,
			req->cbs.userdata);

	if (read_len > 0) {
		/* update stream info */
		req->stream.size += read_len;
		ultotal = req->stream.tsize;
		ulnow = req->stream.size;
		ulpercent = (float)((ulnow / ultotal) * 100);

		/* progress callback */
		(*req->cbs.progress)(req->ctx, req, 0, 0, 0,
				req->stream.tsize, req->stream.size, ulpercent,
				req->cbs.userdata);
	}

	return read_len;
}

static void seq_get_file_size_cb(struct arsdk_ftp_seq *seq,
		size_t size,
		void *userdata)
{
	struct arsdk_ftp_req *req = userdata;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);

	req->stream.tsize = size;
}

static void seq_socket_cb(struct arsdk_ftp_seq *seq, int fd, void *userdata)
{
	struct arsdk_ftp_req *req = userdata;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);

	/* socket hook callback */
	(*req->ctx->cbs.socketcb)(req->ctx, fd, ARSDK_SOCKET_KIND_FTP,
			req->ctx->cbs.userdata);
}

static struct arsdk_ftp_seq_cbs s_seq_cbs = {
	.complete = &seq_complete_cb,
	.data_recv = &seq_data_recv_cb,
	.data_send = &seq_data_send_cb,
	.file_size = &seq_get_file_size_cb,
	.socketcb = &seq_socket_cb,
	.userdata = NULL,
};

static int get_conn_elem(struct arsdk_ftp *ctx, struct sockaddr *addr,
		size_t addrlen, struct arsdk_ftp_conn_elem **ret_elem)
{
	int res = 0;
	struct arsdk_ftp_conn_elem *elem = NULL;
	struct arsdk_ftp_conn_elem *pos = NULL;
	struct arsdk_ftp_conn_elem *tmp = NULL;
	struct arsdk_ftp_conn_cbs conn_cbs;
	const struct sockaddr *idel_addr = NULL;
	uint32_t idel_addrlen = 0;

	list_walk_entry_forward_safe(&ctx->conns_idle, pos, tmp, node) {
		idel_addr = arsdk_ftp_conn_get_addr(pos->conn, &idel_addrlen);

		if ((idel_addr == NULL) || (addrlen != idel_addrlen))
			continue;

		res = memcmp(addr, idel_addr, addrlen);
		if (res == 0) {
			elem = pos;
			list_del(&elem->node);
			break;
		}
	}

	if (elem == NULL) {
		/* Create connection */
		res = conn_elem_new(ctx, addr, addrlen, &elem);
		if (res < 0)
			return res;

		/* Connection callback initialization */
		memset(&conn_cbs, 0, sizeof(conn_cbs));
		conn_cbs.connected = &connected_cb;
		conn_cbs.disconnected = &disconnected_cb;
		conn_cbs.recv_response = &recv_response_cb;
		conn_cbs.socketcb = &conn_socket_cb;
		conn_cbs.userdata = elem;

		res = arsdk_ftp_conn_add_listener(elem->conn, &conn_cbs);
		if (res < 0)
			goto error;
	}
	/* add connection element as busy */
	list_add_before(&ctx->conns_busy, &elem->node);

	*ret_elem = elem;
	return 0;
error:
	conn_elem_destroy(elem);
	return res;
}

/**
 */
static int req_new(struct arsdk_ftp *ctx,
		const struct arsdk_ftp_req_cbs *cbs,
		const char *url,
		enum arsdk_ftp_req_type type,
		struct arsdk_ftp_req **ret_req)
{
	int res = 0;
	struct sockaddr_in addr;
	struct arsdk_ftp_req *req = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(url != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->read_data != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->write_data != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->progress != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->complete != NULL, -EINVAL);

	/* Get address from url */
	memset(&addr, 0, sizeof(addr));
	res = url_to_addr(url, &addr);
	if (res < 0)
		return res;

	req = calloc(1, sizeof(*req));
	if (req == NULL)
		return -ENOMEM;

	req->ctx = ctx;
	req->cbs = *cbs;
	req->type = type;
	req->url = xstrdup(url);
	if (req->url == NULL) {
		res = -ENOMEM;
		goto error;
	}

	/* get */
	res = get_conn_elem(ctx, (struct sockaddr *)&addr, sizeof(addr),
			&req->conn_elem);
	if (res < 0)
		goto error;

	list_add_after(&ctx->requests, &req->node);
	*ret_req = req;
	return 0;

error:
	req_destroy(req);
	return res;
}

static int create_get_seq(struct arsdk_ftp_req *req, const char *path,
		uint64_t resume_off, struct arsdk_ftp_seq **ret_seq)
{
	int res = 0;
	struct arsdk_ftp_seq_cbs seq_cbs = s_seq_cbs;
	struct arsdk_ftp_seq *seq = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->ctx != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_seq != NULL, -EINVAL);

	/* Create command sequence */
	seq_cbs.userdata = req;
	res = arsdk_ftp_seq_new(req->ctx->loop, req->conn_elem->conn,
			&seq_cbs, &seq);
	if (res < 0)
		return res;

	res = arsdk_ftp_seq_append(seq, &ARSDK_FTP_CMD_EPSV, "");
	if (res < 0)
		goto error;

	res = arsdk_ftp_seq_append(seq, &ARSDK_FTP_CMD_TYPE, "I");
	if (res < 0)
		goto error;

	res = arsdk_ftp_seq_append(seq, &ARSDK_FTP_CMD_SIZE, path);
	if (res < 0)
		goto error;

	if (resume_off > 0) {
		res = arsdk_ftp_seq_append_uint64(seq, &ARSDK_FTP_CMD_REST,
				resume_off);
		if (res < 0)
			goto error;
	}

	res = arsdk_ftp_seq_append(seq, &ARSDK_FTP_CMD_RETR, path);
	if (res < 0)
		goto error;

	res = arsdk_ftp_seq_start(seq);
	if (res < 0)
		goto error;

	*ret_seq = seq;
	return 0;
error:
	arsdk_ftp_seq_destroy(seq);
	return res;
}

int arsdk_ftp_get(struct arsdk_ftp *ctx,
		const struct arsdk_ftp_req_cbs *cbs,
		const char *url,
		int64_t resume_off,
		struct arsdk_ftp_req **ret_req)
{
	int res = 0;
	struct arsdk_ftp_req *req = NULL;
	char *path = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ctx != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(url != NULL, -EINVAL);

	/* Create request */
	res = req_new(ctx, cbs, url, ARSDK_FTP_REQ_TYPE_GET, &req);
	if (res < 0)
		return res;

	/* get path */
	/* search "/" after "ftp://" */
	/* url_src validity is checked by req_new() */
	path = strchr(url + 6, '/');
	if (path == NULL) {
		res = -EINVAL;
		goto error;
	}

	/* Set downloaded size*/
	req->stream.size = resume_off;
	/* Create get sequence */
	res = create_get_seq(req, path, resume_off, &req->ftp_seq);
	if (res < 0)
		goto error;

	/* success */
	/* cleanup */
	*ret_req = req;
	return 0;

	/* error */
error:
	/* cleanup */
	req_destroy(req);

	return res;
}

static int create_put_seq(struct arsdk_ftp_req *req, const char *path,
		uint64_t resume_off, struct arsdk_ftp_seq **ret_seq)
{
	int res = 0;
	struct arsdk_ftp_seq_cbs seq_cbs = s_seq_cbs;
	struct arsdk_ftp_seq *seq = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->ctx != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_seq != NULL, -EINVAL);

	/* Create command sequence */
	seq_cbs.userdata = req;
	res = arsdk_ftp_seq_new(req->ctx->loop, req->conn_elem->conn,
			&seq_cbs, &seq);
	if (res < 0)
		return res;

	res = arsdk_ftp_seq_append(seq, &ARSDK_FTP_CMD_EPSV, "");
	if (res < 0)
		goto error;

	res = arsdk_ftp_seq_append(seq, &ARSDK_FTP_CMD_TYPE, "I");
	if (res < 0)
		goto error;

	if (resume_off > 0) {
		res = arsdk_ftp_seq_append_uint64(seq, &ARSDK_FTP_CMD_REST,
				resume_off);
		if (res < 0)
			goto error;

		res = arsdk_ftp_seq_append(seq, &ARSDK_FTP_CMD_APPE, path);
		if (res < 0)
			goto error;
	} else {
		res = arsdk_ftp_seq_append(seq, &ARSDK_FTP_CMD_STOR, path);
		if (res < 0)
			goto error;
	}

	res = arsdk_ftp_seq_start(seq);
	if (res < 0)
		goto error;

	*ret_seq = seq;
	return 0;
error:
	arsdk_ftp_seq_destroy(seq);
	return res;
}

int arsdk_ftp_put(struct arsdk_ftp *ctx,
		const struct arsdk_ftp_req_cbs *cbs,
		const char *url,
		int64_t resume_off,
		int64_t in_size,
		struct arsdk_ftp_req **ret_req)
{
	int res = 0;
	struct arsdk_ftp_req *req = NULL;
	char *path = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ctx != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(url != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(in_size > 0, -EINVAL);

	/* Create request */
	res = req_new(ctx, cbs, url, ARSDK_FTP_REQ_TYPE_PUT, &req);
	if (res < 0)
		return res;

	/* get path */
	/* search "/" after "ftp://" */
	/* url_src validity is checked by req_new() */
	path = strchr(url + 6, '/');
	if (path == NULL) {
		res = -EINVAL;
		goto error;
	}

	/* Set stream info */
	req->stream.tsize = in_size;
	req->stream.size = resume_off;
	/* Create put sequence */
	res = create_put_seq(req, path, resume_off, &req->ftp_seq);
	if (res < 0)
		goto error;

	/* success */
	/* cleanup */
	*ret_req = req;
	return 0;

	/* error */
error:
	/* cleanup */
	req_destroy(req);

	return res;
}

static int create_size_seq(struct arsdk_ftp_req *req, const char *path,
		struct arsdk_ftp_seq **ret_seq)
{
	int res = 0;
	struct arsdk_ftp_seq_cbs seq_cbs = s_seq_cbs;
	struct arsdk_ftp_seq *seq = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->ctx != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_seq != NULL, -EINVAL);

	/* Create command sequence */
	seq_cbs.userdata = req;
	res = arsdk_ftp_seq_new(req->ctx->loop, req->conn_elem->conn,
			&seq_cbs, &seq);
	if (res < 0)
		return res;

	res = arsdk_ftp_seq_append(seq, &ARSDK_FTP_CMD_SIZE, path);
	if (res < 0)
		goto error;

	res = arsdk_ftp_seq_start(seq);
	if (res < 0)
		goto error;

	*ret_seq = seq;
	return 0;
error:
	arsdk_ftp_seq_destroy(seq);
	return res;
}

int arsdk_ftp_size(struct arsdk_ftp *ctx,
		const struct arsdk_ftp_req_cbs *cbs,
		const char *url,
		struct arsdk_ftp_req **ret_req)
{
	int res = 0;
	struct arsdk_ftp_req *req = NULL;
	char *path = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ctx != NULL, -EINVAL);

	/* Create request */
	res = req_new(ctx, cbs, url, ARSDK_FTP_REQ_TYPE_SIZE, &req);
	if (res < 0)
		return res;

	/* get path */
	/* search "/" after "ftp://" */
	/* url_src validity is checked by req_new() */
	path = strchr(url + 6, '/');
	if (path == NULL) {
		res = -EINVAL;
		goto error;
	}

	/* Create size sequence */
	res = create_size_seq(req, path, &req->ftp_seq);
	if (res < 0)
		goto error;

	/* success */
	/* cleanup */

	*ret_req = req;
	return 0;

	/* error */
error:
	/* cleanup */
	req_destroy(req);

	return res;
}

static int create_rename_seq(struct arsdk_ftp_req *req,
		const char *path, const char *org, const char *dst,
		struct arsdk_ftp_seq **ret_seq)
{
	int res = 0;
	struct arsdk_ftp_seq_cbs seq_cbs = s_seq_cbs;
	struct arsdk_ftp_seq *seq = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->ctx != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_seq != NULL, -EINVAL);

	/* Create command sequence */
	seq_cbs.userdata = req;
	res = arsdk_ftp_seq_new(req->ctx->loop, req->conn_elem->conn,
			&seq_cbs, &seq);
	if (res < 0)
		return res;

	res = arsdk_ftp_seq_append(seq, &ARSDK_FTP_CMD_CWD, path);
	if (res < 0)
		goto error;

	res = arsdk_ftp_seq_append(seq, &ARSDK_FTP_CMD_RNFR, org);
	if (res < 0)
		goto error;

	res = arsdk_ftp_seq_append(seq, &ARSDK_FTP_CMD_RNTO, dst);
	if (res < 0)
		goto error;

	res = arsdk_ftp_seq_start(seq);
	if (res < 0)
		goto error;

	*ret_seq = seq;
	return 0;
error:
	arsdk_ftp_seq_destroy(seq);
	return res;
}

int arsdk_ftp_rename(struct arsdk_ftp *ctx,
		const struct arsdk_ftp_req_cbs *cbs,
		const char *url_src,
		const char *dst,
		struct arsdk_ftp_req **ret_req)
{
	int res = 0;
	struct arsdk_ftp_req *req = NULL;
	char *path = NULL;
	char *dir_path = NULL;
	char *splitter = NULL;
	char *src = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ctx != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(url_src != NULL, -EINVAL);

	/* Create request */
	res = req_new(ctx, cbs, url_src, ARSDK_FTP_REQ_TYPE_RENAME, &req);
	if (res < 0)
		return res;

	/* get path */
	/* search "/" after "ftp://" */
	/* url_src validity is checked by req_new() */
	path = strchr(url_src + 6, '/');
	if (path == NULL) {
		res = -EINVAL;
		goto error;
	}

	/* Split url and the last element name */
	dir_path = xstrdup(path);
	splitter = strrchr(dir_path, '/');
	if (splitter == NULL) {
		res = -EINVAL;
		goto error;
	}

	if (*(splitter + 1) == '\0') {
		/* is a directory */
		for (splitter = splitter-1; splitter > dir_path; splitter--) {
			if (*splitter == '/')
				break;
		}
		if (splitter == dir_path) {
			res = -EINVAL;
			goto error;
		}
	}

	src = xstrdup(splitter + 1);
	*(splitter + 1) = '\0';

	/* Create rename sequence */
	res = create_rename_seq(req, dir_path, src, dst, &req->ftp_seq);
	if (res < 0)
		goto error;

	/* success */
	/* cleanup */
	free(dir_path);
	free(src);

	*ret_req = req;
	return 0;

	/* error */
error:
	/* cleanup */
	free(dir_path);
	free(src);
	req_destroy(req);

	return res;
}

static int create_delete_seq(struct arsdk_ftp_req *req, const char *path,
		struct arsdk_ftp_seq **ret_seq)
{
	int res = 0;
	struct arsdk_ftp_seq_cbs seq_cbs = s_seq_cbs;
	struct arsdk_ftp_seq *seq = NULL;
	size_t len = 0;
	const char *last_char = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->ctx != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_seq != NULL, -EINVAL);

	/* Create command sequence */
	seq_cbs.userdata = req;
	res = arsdk_ftp_seq_new(req->ctx->loop, req->conn_elem->conn,
			&seq_cbs, &seq);
	if (res < 0)
		return res;

	len = strlen(path);
	last_char = path + len - 1;
	if (*last_char == '/') {
		res = arsdk_ftp_seq_append(seq, &ARSDK_FTP_CMD_RMD, path);
		if (res < 0)
			goto error;
	} else {
		res = arsdk_ftp_seq_append(seq, &ARSDK_FTP_CMD_DELE, path);
		if (res < 0)
			goto error;
	}

	res = arsdk_ftp_seq_start(seq);
	if (res < 0)
		goto error;

	*ret_seq = seq;
	return 0;
error:
	arsdk_ftp_seq_destroy(seq);
	return res;
}

int arsdk_ftp_delete(struct arsdk_ftp *ctx,
		const struct arsdk_ftp_req_cbs *cbs,
		const char *url,
		struct arsdk_ftp_req **ret_req)
{
	int res = 0;
	struct arsdk_ftp_req *req = NULL;
	char *path = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ctx != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(url != NULL, -EINVAL);

	/* Create request */
	res = req_new(ctx, cbs, url, ARSDK_FTP_REQ_TYPE_DELETE, &req);
	if (res < 0)
		return res;

	/* get path */
	/* search "/" after "ftp://" */
	/* url_src validity is checked by req_new() */
	path = strchr(url + 6, '/');
	if (path == NULL) {
		res = -EINVAL;
		goto error;
	}

	/* Create delete sequence */
	res = create_delete_seq(req, path, &req->ftp_seq);
	if (res < 0)
		goto error;

	/* success */
	/* cleanup */
	*ret_req = req;
	return 0;

	/* error */
error:
	/* cleanup */
	req_destroy(req);
	return res;
}

static int create_list_seq(struct arsdk_ftp_req *req, const char *path,
		struct arsdk_ftp_seq **ret_seq)
{
	int res = 0;
	struct arsdk_ftp_seq_cbs seq_cbs = s_seq_cbs;
	struct arsdk_ftp_seq *seq = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->ctx != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_seq != NULL, -EINVAL);

	/* Create command sequence */
	seq_cbs.userdata = req;
	res = arsdk_ftp_seq_new(req->ctx->loop, req->conn_elem->conn,
			&seq_cbs, &seq);
	if (res < 0)
		return res;

	res = arsdk_ftp_seq_append(seq, &ARSDK_FTP_CMD_EPSV, "");
	if (res < 0)
		goto error;

	res = arsdk_ftp_seq_append(seq, &ARSDK_FTP_CMD_TYPE, "A");
	if (res < 0)
		goto error;

	res = arsdk_ftp_seq_append(seq, &ARSDK_FTP_CMD_LIST, path);
	if (res < 0)
		goto error;

	res = arsdk_ftp_seq_start(seq);
	if (res < 0)
		goto error;

	*ret_seq = seq;
	return 0;
error:
	arsdk_ftp_seq_destroy(seq);
	return res;
}

int arsdk_ftp_list(struct arsdk_ftp *ctx,
		const struct arsdk_ftp_req_cbs *cbs,
		const char *url,
		struct arsdk_ftp_req **ret_req)
{
	int res = 0;
	struct arsdk_ftp_req *req = NULL;
	char *path = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ctx != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(url != NULL, -EINVAL);

	/* Create request */
	res = req_new(ctx, cbs, url, ARSDK_FTP_REQ_TYPE_LIST, &req);
	if (res < 0)
		return res;

	/* get path */
	/* search "/" after "ftp://" */
	/* url_src validity is checked by req_new() */
	path = strchr(url + 6, '/');
	if (path == NULL) {
		res = -EINVAL;
		goto error;
	}

	/* Create list sequence */
	res = create_list_seq(req, path, &req->ftp_seq);
	if (res < 0)
		goto error;

	/* success */
	/* cleanup */
	*ret_req = req;
	return 0;

	/* error */
error:
	/* cleanup */
	req_destroy(req);

	return res;
}

size_t arsdk_ftp_req_get_size(struct arsdk_ftp_req *req)
{
	return (req != NULL) ? req->stream.tsize : 0;
}

const char *arsdk_ftp_req_get_url(struct arsdk_ftp_req *req)
{
	return (req != NULL) ? req->url : NULL;
}

int arsdk_ftp_cancel_req(struct arsdk_ftp *ctx,
			 struct arsdk_ftp_req *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	return arsdk_ftp_seq_stop(req->ftp_seq);
}

int arsdk_ftp_cancel_all(struct arsdk_ftp *ctx)
{
	struct arsdk_ftp_req *req = NULL;
	struct arsdk_ftp_req *tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ctx != NULL, -EINVAL);

	/* cancel all pending requests */
	list_walk_entry_forward_safe(&ctx->requests, req, tmp, node) {
		arsdk_ftp_cancel_req(ctx, req);
	}

	return 0;
}

static int arsdk_ftp_abort_all(struct arsdk_ftp *ctx)
{
	struct arsdk_ftp_req *req = NULL;
	struct arsdk_ftp_req *tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ctx != NULL, -EINVAL);

	/* cancel all pending requests */
	list_walk_entry_forward_safe(&ctx->requests, req, tmp, node) {
		req->is_aborted = 1;
		arsdk_ftp_cancel_req(ctx, req);
	}

	return 0;
}

static int arsdk_ftp_stop_conns(struct arsdk_ftp *ctx)
{
	struct arsdk_ftp_conn_elem *elem = NULL;
	struct arsdk_ftp_conn_elem *tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ctx != NULL, -EINVAL);

	list_walk_entry_forward_safe(&ctx->conns_idle, elem, tmp, node) {
		list_del(&elem->node);
		conn_elem_destroy(elem);
	}

	list_walk_entry_forward_safe(&ctx->conns_busy, elem, tmp, node) {
		list_del(&elem->node);
		conn_elem_destroy(elem);
	}

	return 0;
}

int arsdk_ftp_stop(struct arsdk_ftp *ctx)
{
	ARSDK_RETURN_ERR_IF_FAILED(ctx != NULL, -EINVAL);

	arsdk_ftp_abort_all(ctx);
	arsdk_ftp_stop_conns(ctx);
	return 0;
}
