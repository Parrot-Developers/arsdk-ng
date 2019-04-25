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

enum arsdk_ftp_seq_state {
	ARSDK_FTP_SEQ_STATE_INIT = 0,
	ARSDK_FTP_SEQ_STATE_READY,
	ARSDK_FTP_SEQ_STATE_WAIT_RESP,
	ARSDK_FTP_SEQ_STATE_STREAMING,
	ARSDK_FTP_SEQ_STATE_DONE,
};

enum arsdk_ftp_seq_event_type {
	ARSDK_FTP_SEQ_EVENT_TYPE_APPEND_STEP = 0,
	ARSDK_FTP_SEQ_EVENT_TYPE_START,
	ARSDK_FTP_SEQ_EVENT_TYPE_SEND_CMD,
	ARSDK_FTP_SEQ_EVENT_TYPE_RECV_RESP,
	ARSDK_FTP_SEQ_EVENT_TYPE_DATA_STREAM_START,
	ARSDK_FTP_SEQ_EVENT_TYPE_DATA_STREAM_RECV,
	ARSDK_FTP_SEQ_EVENT_TYPE_DATA_STREAM_STOP,

	ARSDK_FTP_SEQ_EVENT_TYPE_CANCEL,
	ARSDK_FTP_SEQ_EVENT_TYPE_FAIL,
	ARSDK_FTP_SEQ_EVENT_TYPE_END,

	ARSDK_FTP_SEQ_EVENT_TYPE_CONNECTION,
	ARSDK_FTP_SEQ_EVENT_TYPE_DISCONNECTION,
};

struct arsdk_ftp_seq_event {
	enum arsdk_ftp_seq_event_type type;
	union {
		struct {
			const struct arsdk_ftp_cmd_desc *desc;
			const char *param;
		} step;
		struct arsdk_ftp_cmd_result *response;

		int fail_error;
		uint8_t is_aborted;

		struct pomp_buffer *stream_buff;
	} data;
};

struct arsdk_ftp_seq_step {
	const struct arsdk_ftp_cmd_desc         *cmd_desc;
	char                                    *param;
	struct list_node                        node;
};

struct arsdk_ftp_seq {
	struct pomp_loop                        *loop;
	struct arsdk_ftp_conn                   *conn;
	enum arsdk_ftp_seq_state                state;
	struct list_node                        steps;
	struct arsdk_ftp_seq_step               *current;
	struct arsdk_ftp_seq_cbs                cbs;
	struct {
		int                             opened;
		struct pomp_ctx                 *ctx;
		struct pomp_buffer              *output_buff;
		int                             resp_226_received;
	} data_stream;
};

#define ARSDK_FTP_SEQ_DATA_FRAG_LEN_MAX 1024

/* forward declaration */
static int process_event(struct arsdk_ftp_seq *seq,
		struct arsdk_ftp_seq_event *event);

static int arsdk_ftp_seq_step_new(const struct arsdk_ftp_cmd_desc *cmd_desc,
		const char *param, struct arsdk_ftp_seq_step **ret_step)
{
	struct arsdk_ftp_seq_step *step = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(cmd_desc != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_step != NULL, -EINVAL);

	step = calloc(1, sizeof(*step));
	if (step == NULL)
		return -ENOMEM;

	step->cmd_desc = cmd_desc;
	step->param = xstrdup(param);

	*ret_step = step;
	return 0;
}

static int arsdk_ftp_seq_step_destroy(struct arsdk_ftp_seq_step *step)
{
	ARSDK_RETURN_ERR_IF_FAILED(step != NULL, -EINVAL);

	free(step->param);
	free(step);

	return 0;
}

static void connected_cb(struct arsdk_ftp_conn *comm, void *userdata)
{
	struct arsdk_ftp_seq *seq = userdata;

	ARSDK_RETURN_IF_FAILED(seq != NULL, -EINVAL);

	struct arsdk_ftp_seq_event event = {
		.type = ARSDK_FTP_SEQ_EVENT_TYPE_CONNECTION,
	};

	process_event(seq, &event);
}

static void disconnected_cb(struct arsdk_ftp_conn *comm, void *userdata)
{
	struct arsdk_ftp_seq *seq = userdata;

	ARSDK_RETURN_IF_FAILED(seq != NULL, -EINVAL);

	struct arsdk_ftp_seq_event event = {
		.type = ARSDK_FTP_SEQ_EVENT_TYPE_DISCONNECTION,
	};

	process_event(seq, &event);
}

static void recv_response_cb(struct arsdk_ftp_conn *comm,
			struct arsdk_ftp_cmd_result *response, void *userdata)
{
	struct arsdk_ftp_seq *seq = userdata;

	ARSDK_RETURN_IF_FAILED(seq != NULL, -EINVAL);

	struct arsdk_ftp_seq_event event = {
		.type = ARSDK_FTP_SEQ_EVENT_TYPE_RECV_RESP,
		.data.response = response,
	};

	process_event(seq, &event);
}

static void conn_socket_cb(struct arsdk_ftp_conn *comm, int fd, void *userdata)
{
	/* do nothing */
}

int arsdk_ftp_seq_new(struct pomp_loop *loop,
		struct arsdk_ftp_conn *conn,
		const struct arsdk_ftp_seq_cbs *cbs,
		struct arsdk_ftp_seq **ret_seq)
{
	int res = 0;
	struct arsdk_ftp_seq *seq = NULL;
	struct arsdk_ftp_conn_cbs conn_cbs;

	ARSDK_RETURN_ERR_IF_FAILED(loop != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(conn != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_seq != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->complete != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->data_recv != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->data_send != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->file_size != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->socketcb != NULL, -EINVAL);

	seq = calloc(1, sizeof(*seq));
	if (seq == NULL)
		return -ENOMEM;

	seq->loop = loop;
	seq->conn = conn;
	seq->cbs = *cbs;
	seq->state = ARSDK_FTP_SEQ_STATE_INIT;
	list_init(&seq->steps);

	memset(&conn_cbs, 0, sizeof(conn_cbs));
	conn_cbs.connected = &connected_cb;
	conn_cbs.disconnected = &disconnected_cb;
	conn_cbs.recv_response = &recv_response_cb;
	conn_cbs.socketcb = &conn_socket_cb;
	conn_cbs.userdata = seq;
	res = arsdk_ftp_conn_add_listener(seq->conn, &conn_cbs);
	if (res < 0)
		goto error;

	*ret_seq = seq;
	return 0;
error:
	arsdk_ftp_seq_destroy(seq);
	return res;
}

int arsdk_ftp_seq_destroy(struct arsdk_ftp_seq *seq)
{
	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);

	if (seq->state != ARSDK_FTP_SEQ_STATE_DONE)
		return -EBUSY;

	free(seq);
	return 0;
}

static struct arsdk_ftp_seq_step *arsdk_ftp_seq_next_step(
		struct arsdk_ftp_seq *seq,
		struct arsdk_ftp_seq_step *prev)
{
	struct list_node *node;
	struct arsdk_ftp_seq_step *next;

	if (seq == NULL)
		return NULL;

	node = list_next(&seq->steps, prev ? &prev->node : &seq->steps);
	if (node == NULL)
		return NULL;

	next = list_entry(node, struct arsdk_ftp_seq_step, node);
	return next;
}

static int on_step_appened(struct arsdk_ftp_seq *seq,
		struct arsdk_ftp_seq_event *event)
{
	int res = 0;
	struct arsdk_ftp_seq_step *step = NULL;
	struct list_node *last = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(event != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(
			event->type == ARSDK_FTP_SEQ_EVENT_TYPE_APPEND_STEP,
			-EINVAL);

	if (seq->state != ARSDK_FTP_SEQ_STATE_INIT)
		return -EBUSY;

	res = arsdk_ftp_seq_step_new(event->data.step.desc,
			event->data.step.param, &step);
	if (res < 0)
		return res;

	last = list_last(&seq->steps);
	if (last == NULL)
		return -EFAULT;

	list_add_after(last, &step->node);
	return 0;
}

static struct arsdk_ftp_seq_event arsdk_ftp_seq_event_fail(int error)
{
	struct arsdk_ftp_seq_event event = {
		.type = ARSDK_FTP_SEQ_EVENT_TYPE_FAIL,
		.data.fail_error = error,
	};

	return event;
}

static int send_next_step(struct arsdk_ftp_seq *seq)
{
	int res = 0;
	struct pomp_buffer *buff = NULL;
	struct arsdk_ftp_seq_event event_end = {
		.type = ARSDK_FTP_SEQ_EVENT_TYPE_END,
	};

	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);

	seq->current = arsdk_ftp_seq_next_step(seq, seq->current);
	if (seq->current == NULL)
		return process_event(seq, &event_end);

	res = arsdk_ftp_cmd_enc(seq->current->cmd_desc, seq->current->param,
			&buff);
	if (res < 0)
		return res;

	res = arsdk_ftp_conn_send(seq->conn, buff);
	pomp_buffer_unref(buff);
	if (res < 0)
		return res;

	seq->state = ARSDK_FTP_SEQ_STATE_WAIT_RESP;
	return 0;
}

static int on_start(struct arsdk_ftp_seq *seq,
		struct arsdk_ftp_seq_event *event)
{
	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(event != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(
			event->type == ARSDK_FTP_SEQ_EVENT_TYPE_START,
			-EINVAL);

	if (seq->state != ARSDK_FTP_SEQ_STATE_INIT)
		return -EBUSY;

	seq->state = ARSDK_FTP_SEQ_STATE_READY;

	if (arsdk_ftp_conn_is_connected(seq->conn))
		return send_next_step(seq);

	return 0;
}

static int get_out_buff(struct arsdk_ftp_seq *seq, void **data, size_t *cap,
		struct pomp_buffer **ret_buff)
{
	int res = 0;
	struct pomp_buffer *buff = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_buff != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);
	buff = seq->data_stream.output_buff;
	ARSDK_RETURN_ERR_IF_FAILED(buff != NULL, -EINVAL);

	res = pomp_buffer_get_data(buff, data, NULL, cap);
	if (res == -EPERM) {
		/* output_buff still used */
		/* replace by a new buffer */
		pomp_buffer_unref(buff);
		buff = pomp_buffer_new(ARSDK_FTP_SEQ_DATA_FRAG_LEN_MAX);
		seq->data_stream.output_buff = buff;
		if (buff == NULL)
			return -ENOMEM;

		res = pomp_buffer_get_data(buff, data, NULL, cap);
		if (res < 0)
			return res;
	} else if (res < 0) {

		return res;
	}

	*ret_buff = buff;
	return 0;
}

static int data_read(struct arsdk_ftp_seq *seq)
{
	int res = 0;
	size_t read_len = 0;
	struct pomp_buffer *buff = NULL;
	void *data = NULL;
	size_t cap = 0;
	struct arsdk_ftp_seq_event event;

	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);

	res = get_out_buff(seq, &data, &cap, &buff);
	if (res < 0)
		goto error;

	/* read callback */
	read_len = (*seq->cbs.data_send)(seq, data, cap, seq->cbs.userdata);
	if (read_len == 0) {
		/* end of read stream*/
		event.type = ARSDK_FTP_SEQ_EVENT_TYPE_DATA_STREAM_STOP;
		process_event(seq, &event);
		return 0;
	}

	/* update buffer length */
	res = pomp_buffer_set_len(buff, read_len);
	if (res < 0)
		goto error;

	/* send the fragment */
	res = pomp_ctx_send_raw_buf(seq->data_stream.ctx, buff);
	if (res < 0)
		goto error;

	return 0;
error:
	/* fail event */
	event = arsdk_ftp_seq_event_fail(res);
	process_event(seq, &event);
	return res;
}

static void pomp_send_cb(struct pomp_ctx *ctx, struct pomp_conn *conn,
		struct pomp_buffer *buf, uint32_t status, void *cookie,
		void *userdata)
{
	data_read(userdata);
}

static void dispatch_data_stream_ctx_destroy_cb(void *userdata)
{
	pomp_ctx_destroy(userdata);
}

static int stop_send_data(struct arsdk_ftp_seq *seq)
{
	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);

	if (seq->data_stream.output_buff != NULL) {
		pomp_buffer_unref(seq->data_stream.output_buff);
		seq->data_stream.output_buff = NULL;
	}

	return 0;
}

static int start_send_data(struct arsdk_ftp_seq *seq)
{
	int res = 0;

	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);

	seq->data_stream.output_buff =
			pomp_buffer_new(ARSDK_FTP_SEQ_DATA_FRAG_LEN_MAX);
	if (seq->data_stream.output_buff == NULL) {
		res = -ENOMEM;
		goto error;
	}

	/* start to send data */
	res = data_read(seq);
	if (res < 0)
		goto error;

	return 0;
error:
	stop_send_data(seq);
	return res;
}

static int close_data_stream(struct arsdk_ftp_seq *seq)
{
	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);

	pomp_ctx_stop(seq->data_stream.ctx);
	/* dispatch stream destroy out of stream ctx */
	pomp_loop_idle_add(seq->loop, &dispatch_data_stream_ctx_destroy_cb,
			seq->data_stream.ctx);
	seq->data_stream.ctx = NULL;

	return 0;
}

static int stop(struct arsdk_ftp_seq *seq,
		enum arsdk_ftp_seq_status status,
		int error)
{
	struct arsdk_ftp_seq_step *step = NULL;
	struct arsdk_ftp_seq_step *tmp = NULL;

	/* stop data stream */
	stop_send_data(seq);

	/* close data stream*/
	close_data_stream(seq);

	/* delete all steps */
	list_walk_entry_forward_safe(&seq->steps, step, tmp, node) {
		list_del(&step->node);
		arsdk_ftp_seq_step_destroy(step);
	}

	/* remove ftp connection */
	arsdk_ftp_conn_remove_listener(seq->conn, seq);
	seq->conn = NULL;

	/* complete callback */
	(*seq->cbs.complete) (seq, status, error, seq->cbs.userdata);
	return 0;
}

static int on_ftp_connection(struct arsdk_ftp_seq *seq,
		struct arsdk_ftp_seq_event *event)
{
	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);

	switch (seq->state) {
	case ARSDK_FTP_SEQ_STATE_READY:
		return send_next_step(seq);
		break;
	case ARSDK_FTP_SEQ_STATE_INIT:
	case ARSDK_FTP_SEQ_STATE_DONE:
		break;
	case ARSDK_FTP_SEQ_STATE_WAIT_RESP:
	case ARSDK_FTP_SEQ_STATE_STREAMING:
	default:
		break;
	}

	return 0;
}

static int on_ftp_disconnection(struct arsdk_ftp_seq *seq,
		struct arsdk_ftp_seq_event *event)
{
	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);

	switch (seq->state) {
	case ARSDK_FTP_SEQ_STATE_READY:
	case ARSDK_FTP_SEQ_STATE_WAIT_RESP:
	case ARSDK_FTP_SEQ_STATE_STREAMING:
	case ARSDK_FTP_SEQ_STATE_INIT:
		seq->state = ARSDK_FTP_SEQ_STATE_DONE;
		return stop(seq, ARSDK_FTP_SEQ_ABORTED, 0);
	case ARSDK_FTP_SEQ_STATE_DONE:
		return -EBUSY;
	default:
		return -EINVAL;
	}
}

static int on_fail(struct arsdk_ftp_seq *seq,
		struct arsdk_ftp_seq_event *event)
{
	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);

	switch (seq->state) {
	case ARSDK_FTP_SEQ_STATE_READY:
	case ARSDK_FTP_SEQ_STATE_WAIT_RESP:
	case ARSDK_FTP_SEQ_STATE_STREAMING:
	case ARSDK_FTP_SEQ_STATE_INIT:
		seq->state = ARSDK_FTP_SEQ_STATE_DONE;
		return stop(seq, ARSDK_FTP_SEQ_FAILED,
				event->data.fail_error);
	case ARSDK_FTP_SEQ_STATE_DONE:
		return -EBUSY;
	default:
		return -EINVAL;
	}
}

static int on_cancel(struct arsdk_ftp_seq *seq,
		struct arsdk_ftp_seq_event *event)
{
	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);

	switch (seq->state) {
	case ARSDK_FTP_SEQ_STATE_READY:
	case ARSDK_FTP_SEQ_STATE_WAIT_RESP:
	case ARSDK_FTP_SEQ_STATE_STREAMING:
	case ARSDK_FTP_SEQ_STATE_INIT:
		seq->state = ARSDK_FTP_SEQ_STATE_DONE;
		return stop(seq, ARSDK_FTP_SEQ_CANCELED, 0);
	case ARSDK_FTP_SEQ_STATE_DONE:
		return -EBUSY;
	default:
		return -EINVAL;
	}
}

static int on_end(struct arsdk_ftp_seq *seq,
		struct arsdk_ftp_seq_event *event)
{
	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);

	switch (seq->state) {
	case ARSDK_FTP_SEQ_STATE_READY:
	case ARSDK_FTP_SEQ_STATE_WAIT_RESP:
	case ARSDK_FTP_SEQ_STATE_STREAMING:
	case ARSDK_FTP_SEQ_STATE_INIT:
		seq->state = ARSDK_FTP_SEQ_STATE_DONE;
		return stop(seq, ARSDK_FTP_SEQ_STATUS_OK, 0);
	case ARSDK_FTP_SEQ_STATE_DONE:
		return -EBUSY;
	default:
		return -EINVAL;
	}
}

static void data_event_cb(struct pomp_ctx *ctx,
		enum pomp_event pomp_event, struct pomp_conn *sk_conn,
		const struct pomp_msg *msg, void *userdata)
{
	struct arsdk_ftp_seq *seq = userdata;
	struct arsdk_ftp_seq_event event;

	ARSDK_RETURN_IF_FAILED(seq != NULL, -EINVAL);

	switch (pomp_event) {
	case POMP_EVENT_CONNECTED:
		/* data socket stream connected */
		event.type = ARSDK_FTP_SEQ_EVENT_TYPE_DATA_STREAM_START;
		process_event(seq, &event);
		break;
	case POMP_EVENT_DISCONNECTED:
		/* data socket stream disconnected */
		event.type = ARSDK_FTP_SEQ_EVENT_TYPE_DATA_STREAM_STOP;
		process_event(seq, &event);
		break;
	default:
		break;
	}
}

static void data_recv_cb(struct pomp_ctx *ctx,
			struct pomp_conn *sk_conn,
			struct pomp_buffer *buff, void *userdata)
{
	struct arsdk_ftp_seq *seq = userdata;
	struct arsdk_ftp_seq_event event = {
		.type = ARSDK_FTP_SEQ_EVENT_TYPE_DATA_STREAM_RECV,
		.data.stream_buff = buff,
	};

	ARSDK_RETURN_IF_FAILED(seq != NULL, -EINVAL);

	process_event(seq, &event);
}

static void socket_cb(struct pomp_ctx *ctx, int fd, enum pomp_socket_kind kind,
		void *userdata)
{
	struct arsdk_ftp_seq *seq = userdata;

	ARSDK_RETURN_IF_FAILED(seq != NULL, -EINVAL);

	/* socket hook callback */
	(*seq->cbs.socketcb)(seq, fd, seq->cbs.userdata);
}

static int open_data_stream(struct arsdk_ftp_seq *seq,
		struct arsdk_ftp_cmd_result *response)
{
	int res = 0;
	uint32_t addrlen;
	const struct sockaddr *ftp_addr = NULL;
	struct sockaddr data_addr;

	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(response != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(response->code == 229, -EINVAL);

	/* Setup address */
	ftp_addr = arsdk_ftp_conn_get_addr(seq->conn, &addrlen);
	if (ftp_addr == NULL)
		return -EINVAL;
	data_addr = *ftp_addr;
	((struct sockaddr_in *)&data_addr)->sin_port =
			htons(response->param.data_stream_port);

	/* create socket context */
	seq->data_stream.ctx = pomp_ctx_new_with_loop(&data_event_cb, seq,
			seq->loop);
	if (seq->data_stream.ctx == NULL)
		return -ENOMEM;

	res = pomp_ctx_set_socket_cb(seq->data_stream.ctx, &socket_cb);
	if (res < 0)
		goto error;

	/* set raw and disable keepalive */
	pomp_ctx_set_raw(seq->data_stream.ctx, &data_recv_cb);
	pomp_ctx_setup_keepalive(seq->data_stream.ctx, 0, 0, 0, 0);

	res = pomp_ctx_set_send_cb(seq->data_stream.ctx, &pomp_send_cb);
	if (res < 0)
		return res;

	/* try connection */
	res = pomp_ctx_connect(seq->data_stream.ctx, &data_addr,
			sizeof(struct sockaddr_in));
	if (res < 0)
		goto error;

	return 0;
error:
	pomp_ctx_destroy(seq->data_stream.ctx);
	return res;
}

static int manage_response(struct arsdk_ftp_seq *seq,
		struct arsdk_ftp_cmd_result *response)
{

	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(response != NULL, -EINVAL);

	switch (response->code) {
	case 229:
		return open_data_stream(seq, response);
	case 213:
		(*seq->cbs.file_size)(seq, response->param.file_size,
				seq->cbs.userdata);
		return 0;
	default:
		return 0;
	}
}

static int on_resp_recv(struct arsdk_ftp_seq *seq,
		struct arsdk_ftp_seq_event *event)
{
	struct arsdk_ftp_seq_event event_end = {
		.type = ARSDK_FTP_SEQ_EVENT_TYPE_END,
	};
	struct arsdk_ftp_seq_event event_fail;

	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(event != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(
			event->type == ARSDK_FTP_SEQ_EVENT_TYPE_RECV_RESP,
			-EINVAL);

	ARSDK_RETURN_ERR_IF_FAILED(seq->current != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(seq->current->cmd_desc != NULL, -EINVAL);

	if ((seq->state == ARSDK_FTP_SEQ_STATE_STREAMING) &&
	    (event->data.response->code == 226)) {
		seq->data_stream.resp_226_received = 1;
		/* wait the close of the data stream */
		if (seq->data_stream.opened)
			return 0;
		return process_event(seq, &event_end);
	} else if (seq->state != ARSDK_FTP_SEQ_STATE_WAIT_RESP) {
		return -EBUSY;
	}

	if (seq->current->cmd_desc->resp_code != event->data.response->code) {
		/* fail event */
		event_fail = arsdk_ftp_seq_event_fail(
				event->data.response->code);
		return process_event(seq, &event_fail);
	}

	/* manage response */
	manage_response(seq, event->data.response);

	switch (seq->current->cmd_desc->data_type) {
	case ARSDK_FTP_CMD_DATA_TYPE_NONE:
		seq->state = ARSDK_FTP_SEQ_STATE_READY;
		return send_next_step(seq);
	case ARSDK_FTP_CMD_DATA_TYPE_IN:
		if (!seq->data_stream.opened)
			return -EIO;

		seq->state = ARSDK_FTP_SEQ_STATE_STREAMING;
		break;
	case ARSDK_FTP_CMD_DATA_TYPE_OUT:
		if (!seq->data_stream.opened)
			return -EIO;

		seq->state = ARSDK_FTP_SEQ_STATE_STREAMING;
		start_send_data(seq);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int on_data_stream_start(struct arsdk_ftp_seq *seq,
		struct arsdk_ftp_seq_event *event)
{
	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);

	switch (seq->state) {
	case ARSDK_FTP_SEQ_STATE_READY:
	case ARSDK_FTP_SEQ_STATE_WAIT_RESP:
	case ARSDK_FTP_SEQ_STATE_STREAMING:
		if (seq->data_stream.opened)
			return -EBUSY;

		seq->data_stream.opened = 1;
		return 0;
	case ARSDK_FTP_SEQ_STATE_INIT:
	case ARSDK_FTP_SEQ_STATE_DONE:
		return -EBUSY;
	default:
		return -EINVAL;
	}
}

static int on_data_stream_stop(struct arsdk_ftp_seq *seq,
		struct arsdk_ftp_seq_event *event)
{
	struct arsdk_ftp_seq_event event_end = {
		.type = ARSDK_FTP_SEQ_EVENT_TYPE_END,
	};

	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);

	switch (seq->state) {
	case ARSDK_FTP_SEQ_STATE_READY:
	case ARSDK_FTP_SEQ_STATE_WAIT_RESP:
	case ARSDK_FTP_SEQ_STATE_STREAMING:
		if (!seq->data_stream.opened)
			return -EBUSY;

		seq->data_stream.opened = 0;

		/* stop output data stream */
		stop_send_data(seq);

		/* close data stream*/
		close_data_stream(seq);

		/* wait the response code 226 from the server */
		if (!seq->data_stream.resp_226_received)
			return 0;
		else
			return process_event(seq, &event_end);

	case ARSDK_FTP_SEQ_STATE_INIT:
	case ARSDK_FTP_SEQ_STATE_DONE:
		return -EBUSY;
	default:
		return -EINVAL;
	}
}

static int on_data_stream_recv(struct arsdk_ftp_seq *seq,
		struct arsdk_ftp_seq_event *event)
{
	int res = 0;
	struct arsdk_ftp_seq_event event_fail;

	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(event != NULL, -EINVAL);

	/* Don't check if the we are in streaming state */
	/* possibility to receive date before the response code 150 */

	res = (*seq->cbs.data_recv) (seq,
				     event->data.stream_buff,
				     seq->cbs.userdata);
	if (res < 0) {
		/* fail event */
		event_fail = arsdk_ftp_seq_event_fail(res);
		process_event(seq, &event_fail);
		return res;
	}

	return 0;
}

int arsdk_ftp_seq_append(struct arsdk_ftp_seq *seq,
		const struct arsdk_ftp_cmd_desc *desc, const char *param)
{
	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);

	struct arsdk_ftp_seq_event event = {
		.type = ARSDK_FTP_SEQ_EVENT_TYPE_APPEND_STEP,
		.data.step = {
			.desc = desc,
			.param = param,
		},
	};

	return process_event(seq, &event);
}

int arsdk_ftp_seq_start(struct arsdk_ftp_seq *seq)
{
	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);

	struct arsdk_ftp_seq_event event = {
		.type = ARSDK_FTP_SEQ_EVENT_TYPE_START,
	};

	return process_event(seq, &event);
}

int arsdk_ftp_seq_stop(struct arsdk_ftp_seq *seq)
{
	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);

	struct arsdk_ftp_seq_event event = {
		.type = ARSDK_FTP_SEQ_EVENT_TYPE_CANCEL,
	};

	return process_event(seq, &event);
}

static int process_event(struct arsdk_ftp_seq *seq,
		struct arsdk_ftp_seq_event *event)
{
	ARSDK_RETURN_ERR_IF_FAILED(seq != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(event != NULL, -EINVAL);

	switch (event->type) {
	case ARSDK_FTP_SEQ_EVENT_TYPE_APPEND_STEP:
		return on_step_appened(seq, event);
	case ARSDK_FTP_SEQ_EVENT_TYPE_START:
		return on_start(seq, event);
	case ARSDK_FTP_SEQ_EVENT_TYPE_CONNECTION:
		return on_ftp_connection(seq, event);
	case ARSDK_FTP_SEQ_EVENT_TYPE_DISCONNECTION:
		return on_ftp_disconnection(seq, event);
	case ARSDK_FTP_SEQ_EVENT_TYPE_RECV_RESP:
		return on_resp_recv(seq, event);
	case ARSDK_FTP_SEQ_EVENT_TYPE_DATA_STREAM_START:
		return on_data_stream_start(seq, event);
	case ARSDK_FTP_SEQ_EVENT_TYPE_DATA_STREAM_RECV:
		return on_data_stream_recv(seq, event);
	case ARSDK_FTP_SEQ_EVENT_TYPE_DATA_STREAM_STOP:
		return on_data_stream_stop(seq, event);
	case ARSDK_FTP_SEQ_EVENT_TYPE_CANCEL:
		return on_cancel(seq, event);
	case ARSDK_FTP_SEQ_EVENT_TYPE_FAIL:
		return on_fail(seq, event);
	case ARSDK_FTP_SEQ_EVENT_TYPE_END:
		return on_end(seq, event);
	default:
		return -EINVAL;
	}
}
