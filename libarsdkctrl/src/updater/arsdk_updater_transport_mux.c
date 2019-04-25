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

#include "arsdk_updater_itf_priv.h"
#include "updater/arsdk_updater_transport.h"
#include "updater/arsdk_updater_transport_priv.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "arsdk_updater_transport_mux.h"
#include "arsdkctrl_default_log.h"
#ifdef BUILD_LIBMUX
#include "mux/arsdk_mux.h"

#define ARSDK_UPDATER_TRANSPORT_TAG                     "mux"
#define ARSDK_UPDATER_TRANSPORT_MUX_CHUNK_SIZE          (128*1024)

struct arsdk_updater_transport_mux {
	struct arsdk_updater_transport          *parent;
	struct mux_ctx                          *mux;
	struct pomp_loop                        *loop;
	struct list_node                        reqs;
};

struct arsdk_updater_mux_req_upload {
	struct arsdk_updater_transport_mux      *tsprt;
	uint8_t                                 is_aborted;
	uint8_t                                 is_canceled;
	enum arsdk_device_type                  dev_type;
	struct list_node                        node;
	struct arsdk_updater_req_upload         *parent;
	struct arsdk_updater_req_upload_cbs     cbs;
	int                                     fd;
	size_t                                  size;
	size_t                                  n_written;
	void                                    *chunk;
	size_t                                  chunk_id;
	enum arsdk_updater_req_status           status;
	int                                     error;
};

static int stop_cb(struct arsdk_updater_transport *base)
{
	struct arsdk_updater_transport_mux *tsprt =
			arsdk_updater_transport_get_child(base);

	return arsdk_updater_transport_mux_stop(tsprt);
}

static int cancel_all_cb(struct arsdk_updater_transport *base)
{
	struct arsdk_updater_transport_mux *tsprt =
			arsdk_updater_transport_get_child(base);

	return arsdk_updater_transport_mux_cancel_all(tsprt);
}

static int create_req_upload_cb(struct arsdk_updater_transport *base,
		const char *fw_filepath,
		enum arsdk_device_type dev_type,
		const struct arsdk_updater_req_upload_cbs *cbs,
		struct arsdk_updater_req_upload **ret_req)
{
	int res = 0;
	struct arsdk_updater_mux_req_upload *req = NULL;
	struct arsdk_updater_transport_mux *tsprt =
			arsdk_updater_transport_get_child(base);

	res = arsdk_updater_transport_mux_create_req_upload(tsprt,
			fw_filepath,
			dev_type,
			cbs,
			&req);
	if (res < 0)
		return res;

	res = arsdk_updater_new_req_upload(base, req, cbs, dev_type,
			&req->parent);
	if (res < 0)
		return res;

	*ret_req = req->parent;
	return res;
}

static int cancel_req_upload_cb(struct arsdk_updater_transport *base,
		struct arsdk_updater_req_upload *req)
{
	struct arsdk_updater_mux_req_upload *req_mux =
			arsdk_updater_req_upload_child(req);

	return arsdk_updater_mux_req_upload_cancel(req_mux);
}

/** */
static const struct arsdk_updater_transport_ops s_arsdk_updater_tsprt_ops = {
	.stop = &stop_cb,
	.cancel_all = &cancel_all_cb,
	.create_req_upload = &create_req_upload_cb,
	.cancel_req_upload = &cancel_req_upload_cb,
};

int arsdk_updater_transport_mux_new(struct arsdk_updater_itf *itf,
		struct mux_ctx *mux,
		struct arsdk_updater_transport_mux **ret_obj)
{
	int res = 0;
	struct arsdk_updater_transport_mux *self = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(mux != NULL, -EINVAL);

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Initialize structure (make sure socket fds are setup before handling
	 * errors) */
	self->mux = mux;
	mux_ref(self->mux);
	list_init(&self->reqs);

	/* Setup base structure */
	res = arsdk_updater_transport_new(self, ARSDK_UPDATER_TRANSPORT_TAG,
			&s_arsdk_updater_tsprt_ops, itf, &self->parent);
	if (res < 0)
		goto error;

	/* Success */
	*ret_obj = self;
	return 0;

	/* Cleanup in case of error */
error:
	arsdk_updater_transport_destroy(self->parent);
	return res;
}

static int arsdk_updater_req_upload_mux_abort(
		struct arsdk_updater_mux_req_upload *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	req->is_aborted = 1;
	return arsdk_updater_mux_req_upload_cancel(req);
}

static int arsdk_updater_transport_mux_abort_all(
		struct arsdk_updater_transport_mux *tsprt)
{
	struct arsdk_updater_mux_req_upload *req = NULL;
	struct arsdk_updater_mux_req_upload *req_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(tsprt != NULL, -EINVAL);

	list_walk_entry_forward_safe(&tsprt->reqs, req, req_tmp, node) {
		arsdk_updater_req_upload_mux_abort(req);
	}

	return 0;
}

int arsdk_updater_transport_mux_stop(
		struct arsdk_updater_transport_mux *tsprt)
{
	ARSDK_RETURN_ERR_IF_FAILED(tsprt != NULL, -EINVAL);

	arsdk_updater_transport_mux_abort_all(tsprt);

	return 0;
}

int arsdk_updater_transport_mux_cancel_all(
		struct arsdk_updater_transport_mux *tsprt)
{
	struct arsdk_updater_mux_req_upload *req = NULL;
	struct arsdk_updater_mux_req_upload *req_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(tsprt != NULL, -EINVAL);

	list_walk_entry_forward_safe(&tsprt->reqs, req, req_tmp, node) {
		arsdk_updater_mux_req_upload_cancel(req);
	}

	return 0;
}

int arsdk_updater_transport_mux_destroy(
		struct arsdk_updater_transport_mux *tsprt)
{
	ARSDK_RETURN_ERR_IF_FAILED(tsprt != NULL, -EINVAL);

	arsdk_updater_transport_mux_stop(tsprt);

	free(tsprt);
	return 0;
}

static void arsdk_updater_req_upload_destroy(
		struct arsdk_updater_mux_req_upload *req_upload)
{
	ARSDK_RETURN_IF_FAILED(req_upload != NULL, -EINVAL);

	arsdk_updater_destroy_req_upload(req_upload->parent);

	mux_channel_close(req_upload->tsprt->mux, MUX_UPDATE_CHANNEL_ID_UPDATE);
	close(req_upload->fd);
	free(req_upload->chunk);
	free(req_upload);
}

static void update_mux_notify_status(struct arsdk_updater_mux_req_upload *req,
		enum arsdk_updater_req_status status)
{
	struct arsdk_updater_itf *itf = NULL;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(req->tsprt != NULL, -EINVAL);

	itf = arsdk_updater_transport_get_itf(req->tsprt->parent);

	ARSDK_LOGI("[%s] End of firmware upload with status : %d",
				ARSDK_UPDATER_TRANSPORT_TAG,
				status);

	(*req->cbs.complete)(itf, req->parent, status, 0,
				req->cbs.userdata);

	/* cleanup */
	list_del(&req->node);
	arsdk_updater_req_upload_destroy(req);
}

int arsdk_updater_mux_req_upload_cancel(
		struct arsdk_updater_mux_req_upload *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	req->is_canceled = 1;
	update_mux_notify_status(req,
			(req->is_aborted) ?
			ARSDK_UPDATER_REQ_STATUS_ABORTED :
			ARSDK_UPDATER_REQ_STATUS_CANCELED);

	return 0;
}

static int updater_mux_write_msg(struct mux_ctx *mux, uint32_t msgid,
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
		ARSDK_LOG_ERRNO("pomp_msg_writev failed", -res);
		goto out;
	}

	res = mux_encode(mux, MUX_UPDATE_CHANNEL_ID_UPDATE,
			pomp_msg_get_buffer(msg));
	if (res < 0 && res != -EPIPE) {
		ARSDK_LOG_ERRNO("mux_encode failed", -res);
		goto out;
	}

out:
	pomp_msg_destroy(msg);
	return res;
}

static void update_mux_notify_progression(
		struct arsdk_updater_mux_req_upload *req,
		float percent)
{
	struct arsdk_updater_itf *itf = NULL;
	int event = (int)percent;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(req->tsprt != NULL, -EINVAL);

	if (event <= 0)
		return;

	itf = arsdk_updater_transport_get_itf(req->tsprt->parent);

	(*req->cbs.progress)(itf, req->parent, percent, req->cbs.userdata);
}

static int updater_mux_send_next_chunk(struct arsdk_updater_mux_req_upload *req)
{
	ssize_t ret;
	uint32_t n_bytes;

	/* read file chunk */
	ret = read(req->fd, req->chunk, ARSDK_UPDATER_TRANSPORT_MUX_CHUNK_SIZE);
	if (ret < 0) {
		ARSDK_LOG_ERRNO("read update file failed", errno);
		ret = -errno;
		return ret;
	}

	if (ret == 0) {
		ARSDK_LOGI("read update file eof");
		return 0;
	}

	/* send chunk over mux */
	n_bytes = ret;

	ARSDK_LOGI("sending chunk: id=%zu size=%d", req->chunk_id, n_bytes);

	ret = updater_mux_write_msg(req->tsprt->mux, MUX_UPDATE_MSG_ID_CHUNK,
			MUX_UPDATE_MSG_FMT_ENC_CHUNK, req->chunk_id,
			req->chunk, n_bytes);
	if (ret < 0)
		return ret;

	req->n_written += n_bytes;
	return 0;
}

static void updater_mux_channel_recv(
		struct arsdk_updater_mux_req_upload *req,
		struct pomp_buffer *buf)
{
	struct pomp_msg *msg = NULL;
	float percent;
	int ret, status;
	unsigned int id;

	/* Create pomp message from buffer */
	msg = pomp_msg_new_with_buffer(buf);
	if (msg == NULL)
		return;

	/* Decode message */
	switch (pomp_msg_get_id(msg)) {
	case MUX_UPDATE_MSG_ID_UPDATE_RESP:
		/* decode status */
		ret = pomp_msg_read(msg, MUX_UPDATE_MSG_FMT_DEC_UPDATE_RESP,
				&status);

		if (ret < 0) {
			ARSDK_LOG_ERRNO("pomp_msg_read failed", -ret);
			goto error;
		}

		ARSDK_LOGI("update resp: status=%d", status);

		if (status != 0) {
			ARSDK_LOGE("update refused by remote");
			goto error;
		}

		/* update accepted: start sensing file */
		req->n_written = 0;
		req->chunk_id = 0;
		lseek(req->fd, 0, SEEK_SET);

		/* send 1st chunk */
		ret = updater_mux_send_next_chunk(req);
		if (ret < 0)
			goto error;
		break;

	case MUX_UPDATE_MSG_ID_CHUNK_ACK:
		/* decode chunk id */
		ret = pomp_msg_read(msg, MUX_UPDATE_MSG_FMT_DEC_CHUNK_ACK, &id);
		if (ret < 0) {
			ARSDK_LOG_ERRNO("pomp_msg_read failed", -ret);
			goto error;
		}

		ARSDK_LOGI("chunk ack: id=%d", id);

		if (id != req->chunk_id) {
			ARSDK_LOGE("chunk id mismatch %d != %zu",
					id, req->chunk_id);
			goto error;
		}

		/* notify progression */
		percent = (double) (100.f * req->n_written) /
				   (double)req->size;
		ARSDK_LOGI("progression: %f%%", percent);
		update_mux_notify_progression(req, percent);

		/* send next chunk */
		if (req->n_written < req->size) {
			req->chunk_id++;
			ret = updater_mux_send_next_chunk(req);
			if (ret < 0)
				goto error;
		} else {
			/* last chunk sent and ack successfully received
			 * wait for update status from remote */
			ARSDK_LOGI("image sent waiting for status");
		}
		break;

	case MUX_UPDATE_MSG_ID_STATUS:
		/* decode update status */
		ret = pomp_msg_read(msg, MUX_UPDATE_MSG_FMT_DEC_STATUS,
				&status);
		if (ret < 0) {
			ARSDK_LOG_ERRNO("pomp_msg_read failed", -ret);
			goto error;
		}

		ARSDK_LOGI("update status: status=%d", status);

		/* check remote update status is ok */
		if (status != 0) {
			ARSDK_LOGE("remote update status %d", status);
			goto error;
		}

		/* notify upload succeed */
		update_mux_notify_status(req, ARSDK_UPDATER_REQ_STATUS_OK);
		break;

	default:
		ARSDK_LOGE("unsupported update mux msg %d",
				pomp_msg_get_id(msg));
		goto error;
		break;
	}

	pomp_msg_destroy(msg);
	return;

error:
	pomp_msg_destroy(msg);
	update_mux_notify_status(req, ARSDK_UPDATER_REQ_STATUS_FAILED);
	return;
}

static void update_mux_channel_cb(struct mux_ctx *ctx, uint32_t chanid,
		enum mux_channel_event event, struct pomp_buffer *buf,
		void *userdata)
{
	struct arsdk_updater_mux_req_upload *req = userdata;

	/* ignore message if upload has been canceled */
	if (req->is_canceled)
		return;

	switch (event) {
	case MUX_CHANNEL_RESET:
		update_mux_notify_status(req, ARSDK_UPDATER_REQ_STATUS_FAILED);
		break;
	case MUX_CHANNEL_DATA:
		updater_mux_channel_recv(req, buf);
		break;
	}
}

int arsdk_updater_transport_mux_create_req_upload(
		struct arsdk_updater_transport_mux *tsprt,
		const char *fw_filepath,
		enum arsdk_device_type dev_type,
		const struct arsdk_updater_req_upload_cbs *cbs,
		struct arsdk_updater_mux_req_upload **ret_req)
{
	int res = 0;
	struct arsdk_updater_mux_req_upload *req_upload = NULL;
	struct arsdk_updater_fw_info fw_info;

	ARSDK_RETURN_ERR_IF_FAILED(ret_req != NULL, -EINVAL);
	*ret_req = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(fw_filepath != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(tsprt != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->complete != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->progress != NULL, -EINVAL);

	/* Get fw info */
	res = arsdk_updater_read_fw_info(fw_filepath, &fw_info);
	if (res < 0)
		return res;

	/* Compliance check */
	if (!arsdk_updater_fw_dev_comp(&fw_info, dev_type))
		return -EINVAL;

	/* Allocate structure */
	req_upload = calloc(1, sizeof(*req_upload));
	if (req_upload == NULL)
		return -ENOMEM;

	/* Initialize structure */
	req_upload->tsprt = tsprt;
	req_upload->dev_type = dev_type;
	req_upload->cbs = *cbs;
	req_upload->size = fw_info.size;
	req_upload->fd = -1;

	/* allocate chunk buffer */
	req_upload->chunk = malloc(ARSDK_UPDATER_TRANSPORT_MUX_CHUNK_SIZE);
	if (req_upload->chunk == NULL) {
		res = -ENOMEM;
		goto error;
	}

	/* open image file */
	req_upload->fd = open(fw_filepath, O_RDONLY);
	if (req_upload->fd < 0) {
		ARSDK_LOGE("can't open mux update file '%s': error %s",
				fw_filepath,
			strerror(errno));
		res = -errno;
		goto error;
	}

	/* open mux update channel */
	res = mux_channel_open(tsprt->mux, MUX_UPDATE_CHANNEL_ID_UPDATE,
			&update_mux_channel_cb, req_upload);
	if (res < 0) {
		ARSDK_LOG_ERRNO("mux_channel_open failed", -res);
		goto error;
	}

	/* send update request */
	res = updater_mux_write_msg(tsprt->mux, MUX_UPDATE_MSG_ID_UPDATE_REQ,
			MUX_UPDATE_MSG_FMT_ENC_UPDATE_REQ, fw_info.name,
			fw_info.md5, sizeof(fw_info.md5), fw_info.size);
	if (res < 0)
		goto error;

	ARSDK_LOGI("[%s] Start to upload firmware :\n"
			"\t- product:\t0x%04x\n"
			"\t- version:\t%s\n"
			"\t- size:\t\t%zu",
			ARSDK_UPDATER_TRANSPORT_TAG,
			fw_info.devtype,
			fw_info.name,
			fw_info.size);

	list_add_after(&tsprt->reqs, &req_upload->node);
	*ret_req = req_upload;
	return 0;

error:
	arsdk_updater_req_upload_destroy(req_upload);
	return res;
}

struct arsdk_updater_transport *arsdk_updater_transport_mux_get_parent(
		struct arsdk_updater_transport_mux *self)
{
	return self == NULL ? NULL : self->parent;
}
#else /* !BUILD_LIBMUX */

int arsdk_updater_transport_mux_new(struct arsdk_updater_itf *itf,
		struct mux_ctx *mux,
		struct arsdk_updater_transport_mux **ret_obj)
{
	return -ENOSYS;
}

int arsdk_updater_transport_mux_stop(struct arsdk_updater_transport_mux *tsprt)
{
	return -ENOSYS;
}

int arsdk_updater_transport_mux_cancel_all(
		struct arsdk_updater_transport_mux *tsprt)
{
	return -ENOSYS;
}

int arsdk_updater_transport_mux_destroy(
		struct arsdk_updater_transport_mux *tsprt)
{
	return -ENOSYS;
}

int arsdk_updater_mux_req_upload_cancel(
		struct arsdk_updater_mux_req_upload *req)
{
	return -ENOSYS;
}

int arsdk_updater_transport_mux_create_req_upload(
		struct arsdk_updater_transport_mux *tsprt,
		const char *fw_filepath,
		enum arsdk_device_type dev_type,
		const struct arsdk_updater_req_upload_cbs *cbs,
		struct arsdk_updater_mux_req_upload **ret_req)
{
	return -ENOSYS;
}

struct arsdk_updater_transport *arsdk_updater_transport_mux_get_parent(
		struct arsdk_updater_transport_mux *self)
{
	return NULL;
}

#endif /* ! BUILD_LIBMUX */
