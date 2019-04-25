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
#include "arsdkctrl_default_log.h"
#include <ctype.h>

#include <fcntl.h>
#include "arsdk_ephemeris_itf_priv.h"

#define ARSDK_EPHEMERIS_PATH "/internal_000/gps_data/"
#define ARSDK_EPHEMERIS_FILE_NAME "ephemeris.bin"
#define ARSDK_EPHEMERIS_BEBOP_FILE_NAME "eRide_data.bin"

/** */
struct arsdk_ephemeris_itf {
	struct arsdk_device_info                *dev_info;
	struct arsdk_ftp_itf                    *ftp;
	struct list_node                        reqs;
};

/** */
struct arsdk_ephemeris_req_upload {
	struct arsdk_ephemeris_itf              *itf;
	struct arsdk_ephemeris_req_upload_cbs   cbs;
	int                                     is_running;
	enum arsdk_ephemeris_req_status         status;
	enum arsdk_device_type                  dev_type;
	char                                    *local_filepath;
	struct {
		struct arsdk_ftp_req_get        *ftp_get_req;
		struct arsdk_ftp_req_put        *ftp_put_req;
		char                            str[2 * ARSDK_MD5_LENGTH + 1];
		char                            *path;
	} md5;
	struct {
		struct arsdk_ftp_req_put        *ftp_put_req;
		char                            *path;
	} eph;
	struct list_node                        node;
};

int arsdk_ephemeris_itf_new(struct arsdk_device_info *dev_info,
		struct arsdk_ftp_itf *ftp_itf,
		struct arsdk_ephemeris_itf **ret_itf)
{
	struct arsdk_ephemeris_itf *itf = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_itf != NULL, -EINVAL);
	*ret_itf = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(dev_info != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ftp_itf != NULL, -EINVAL);

	/* Allocate structure */
	itf = calloc(1, sizeof(*itf));
	if (itf == NULL)
		return -ENOMEM;

	/* Initialize structure */
	itf->dev_info = dev_info;
	itf->ftp = ftp_itf;
	list_init(&itf->reqs);

	*ret_itf = itf;
	return 0;
}

static int arsdk_ephemeris_req_upload_abort(
		struct arsdk_ephemeris_req_upload *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	req->status = ARSDK_EPHEMERIS_REQ_STATUS_ABORTED;
	return arsdk_ephemeris_req_upload_cancel(req);
}

static int arsdk_ephemeris_abort_all(struct arsdk_ephemeris_itf *itf)
{
	struct arsdk_ephemeris_req_upload *req = NULL;
	struct arsdk_ephemeris_req_upload *req_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	list_walk_entry_forward_safe(&itf->reqs, req, req_tmp, node) {
		arsdk_ephemeris_req_upload_abort(req);
	}

	return 0;
}

int arsdk_ephemeris_itf_stop(struct arsdk_ephemeris_itf *itf)
{
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	arsdk_ephemeris_abort_all(itf);

	return 0;
}

int arsdk_ephemeris_itf_cancel_all(struct arsdk_ephemeris_itf *itf)
{
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	struct arsdk_ephemeris_req_upload *req = NULL;
	struct arsdk_ephemeris_req_upload *req_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	list_walk_entry_forward_safe(&itf->reqs, req, req_tmp, node) {
		arsdk_ephemeris_req_upload_cancel(req);
	}

	return 0;
}

int arsdk_ephemeris_itf_destroy(struct arsdk_ephemeris_itf *itf)
{
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	arsdk_ephemeris_itf_stop(itf);

	free(itf);
	return 0;
}

/**
 * Destroy ephemeris request.
 * @param req : ephemeris request.
 * @return 0 in case of success, negative errno value in case of error.
 */
static int arsdk_ephemeris_destroy_req_upload(
		struct arsdk_ephemeris_req_upload *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	free(req->eph.path);
	free(req->md5.path);
	free(req->local_filepath);
	free(req);
	return 0;
}

/**
 * end of a ephemeris req run.
 */
static void req_done(struct arsdk_ephemeris_req_upload *req,
		enum arsdk_ephemeris_req_status status, int error)
{
	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);

	(*req->cbs.complete)(req->itf, req, status, error,
			req->cbs.userdata);

	if (req->is_running) {
		list_del(&req->node);
		arsdk_ephemeris_destroy_req_upload(req);
	}
}

static void put_progress(struct arsdk_ftp_itf *itf,
		struct arsdk_ftp_req_put *req_ftp,
		float percent,
		void *userdata)
{
	struct arsdk_ephemeris_req_upload *req = userdata;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);

	if (req_ftp == req->eph.ftp_put_req)
		(*req->cbs.progress)(req->itf, req, percent, req->cbs.userdata);
}

static enum arsdk_ephemeris_req_status ftp_to_ephemeris_status(
		enum arsdk_ftp_req_status ftp_status)
{
	switch (ftp_status) {
	case ARSDK_FTP_REQ_STATUS_OK:
		return ARSDK_EPHEMERIS_REQ_STATUS_OK;
	case ARSDK_FTP_REQ_STATUS_FAILED:
		return ARSDK_EPHEMERIS_REQ_STATUS_FAILED;
	case ARSDK_FTP_REQ_STATUS_CANCELED:
		return ARSDK_EPHEMERIS_REQ_STATUS_CANCELED;
	case ARSDK_FTP_REQ_STATUS_ABORTED:
		return ARSDK_EPHEMERIS_REQ_STATUS_ABORTED;
	default:
		ARSDK_LOGE("ftp status not known. %d", ftp_status);
		return ARSDK_EPHEMERIS_REQ_STATUS_FAILED;
	}
}

static void put_complete(struct arsdk_ftp_itf *itf,
		struct arsdk_ftp_req_put *req_ftp,
		enum arsdk_ftp_req_status status,
		int error,
		void *userdata)
{
	struct arsdk_ephemeris_req_upload *req = userdata;
	int res = 0;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);

	if (req->status != ARSDK_EPHEMERIS_REQ_STATUS_OK)
		goto done;
	if (status != ARSDK_FTP_REQ_STATUS_OK) {
		req->status = ftp_to_ephemeris_status(status);
		res = error;
		goto done;
	}

done:
	if (req_ftp == req->md5.ftp_put_req)
		req->md5.ftp_put_req = NULL;
	else if (req_ftp == req->eph.ftp_put_req)
		req->eph.ftp_put_req = NULL;

	if (req->md5.ftp_put_req != NULL || req->eph.ftp_put_req != NULL)
		return;

	req_done(req, req->status, res);
}

static int put_files(struct arsdk_ephemeris_req_upload *req)
{
	int res = 0;
	struct arsdk_ftp_req_put_cbs md5_get_cbs = {
		.progress = put_progress,
		.complete = put_complete,
		.userdata = req,
	};
	struct pomp_buffer *buff = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	/* send files */
	res = arsdk_ftp_itf_create_req_put(req->itf->ftp, &md5_get_cbs,
			req->dev_type, ARSDK_FTP_SRV_TYPE_MEDIA,
			req->eph.path, req->local_filepath, 0,
			&req->eph.ftp_put_req);
	if (res < 0)
		goto error;

	buff = pomp_buffer_new_with_data(req->md5.str, strlen(req->md5.str));
	if (buff == NULL) {
		res = -ENOMEM;
		goto error;
	}

	res = arsdk_ftp_itf_create_req_put_buff(req->itf->ftp, &md5_get_cbs,
			req->dev_type, ARSDK_FTP_SRV_TYPE_MEDIA,
			req->md5.path, buff, 0,
			&req->md5.ftp_put_req);
	pomp_buffer_unref(buff);
	if (res < 0)
		goto error;

	return 0;

error:
	if (req->eph.ftp_put_req != NULL)
		arsdk_ftp_req_put_cancel(req->eph.ftp_put_req);
	else
		req_done(req, ARSDK_EPHEMERIS_REQ_STATUS_FAILED, res);
	return res;
}

static void md5_get_progress(struct arsdk_ftp_itf *itf,
		struct arsdk_ftp_req_get *ftp_req,
		float percent,
		void *userdata)
{
	/* do nothing */
}

static int cmp_md5s(struct pomp_buffer *remote_buff,
		char *local_str, size_t local_len) {
	int res = 0;
	char *remote_str = NULL;
	size_t remote_len = 0;

	ARSDK_RETURN_ERR_IF_FAILED(remote_buff != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(local_str != NULL, -EINVAL);

	res = pomp_buffer_get_data(remote_buff, (void **)&remote_str,
					&remote_len, NULL);
	if (res < 0)
		return res;

	if (local_len != remote_len)
		return 0;

	/* compare md5 */
	res = strncmp(remote_str, local_str, local_len);
	return (res == 0);
}

static void md5_get_complete(struct arsdk_ftp_itf *itf,
		struct arsdk_ftp_req_get *ftp_req,
		enum arsdk_ftp_req_status status,
		int error,
		void *userdata)
{
	int same_md5 = 0;
	struct arsdk_ephemeris_req_upload *req = userdata;
	struct pomp_buffer *remote_buff = NULL;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);

	if ((req->status == ARSDK_EPHEMERIS_REQ_STATUS_OK) &&
	    (status == ARSDK_FTP_REQ_STATUS_OK)) {
		remote_buff = arsdk_ftp_req_get_get_buffer(ftp_req);
		if (remote_buff == NULL)
			goto end;

		same_md5 = cmp_md5s(remote_buff, req->md5.str,
				strlen(req->md5.str));
	}

end:
	req->md5.ftp_get_req = NULL;

	if ((same_md5) || (!req->is_running)) {
		req->status = ftp_to_ephemeris_status(status);
		req_done(req, req->status, 0);
	} else {
		put_files(req);
	}
}

static int compute_md5_str(const char *eph_filepath, char *str, size_t len)
{
	int res = 0;
	int fd = -1;
	uint8_t md5_data[ARSDK_MD5_LENGTH];

	ARSDK_RETURN_ERR_IF_FAILED(str != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(eph_filepath != NULL, -EINVAL);

	fd = open(eph_filepath, O_RDONLY);
	if (fd < 0) {
		res = -errno;
		goto end;
	}

	/* compute ephemeris md5 */
	res = arsdk_md5_compute(fd, md5_data);
	if (res < 0)
		goto end;

	arsdk_md5_to_str(md5_data, str, len);

end:
	if (fd != -1) {
		close(fd);
		fd = -1;
	}
	return res;
}

int arsdk_ephemeris_itf_create_req_upload(
		struct arsdk_ephemeris_itf *itf,
		const char *eph_filepath,
		enum arsdk_device_type dev_type,
		const struct arsdk_ephemeris_req_upload_cbs *cbs,
		struct arsdk_ephemeris_req_upload **ret_req)
{
	int res = 0;
	const char *eph_file_name = ARSDK_EPHEMERIS_FILE_NAME;
	struct arsdk_ephemeris_req_upload *req = NULL;
	struct arsdk_ftp_req_get_cbs md5_get_cbs = {
		.progress = md5_get_progress,
		.complete = md5_get_complete,
	};

	ARSDK_RETURN_ERR_IF_FAILED(ret_req != NULL, -EINVAL);
	*ret_req = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(eph_filepath != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->progress != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->complete != NULL, -EINVAL);

	/* Allocate structure */
	req = calloc(1, sizeof(*req));
	if (req == NULL)
		return -ENOMEM;

	/* Initialize structure */
	req->itf = itf;
	req->cbs = *cbs;
	req->dev_type = dev_type;
	req->is_running = 1;
	req->status = ARSDK_EPHEMERIS_REQ_STATUS_OK;
	req->local_filepath = xstrdup(eph_filepath);
	if (req->local_filepath == NULL) {
		res = -ENOMEM;
		goto error;
	}

	/* set ephemeris remote files paths */
	if (dev_type == ARSDK_DEVICE_TYPE_BEBOP)
		eph_file_name = ARSDK_EPHEMERIS_BEBOP_FILE_NAME;

	res = asprintf(&req->eph.path, "%s%s", ARSDK_EPHEMERIS_PATH,
			ARSDK_EPHEMERIS_FILE_NAME);
	if (res < 0) {
		res = -ENOMEM;
		goto error;
	}

	res = asprintf(&req->md5.path, "%s.md5", req->eph.path);
	if (res < 0) {
		res = -ENOMEM;
		goto error;
	}

	/* compute ephemeris md5 */
	res = compute_md5_str(req->local_filepath, req->md5.str,
			sizeof(req->md5.str));
	if (res < 0)
		goto error;

	/* Get ephemeris md5 on device */
	md5_get_cbs.userdata = req;
	res = arsdk_ftp_itf_create_req_get(itf->ftp, &md5_get_cbs, dev_type,
			ARSDK_FTP_SRV_TYPE_MEDIA, req->md5.path,
			NULL, 0, &req->md5.ftp_get_req);

	if (res < 0)
		goto error;

	list_add_after(&itf->reqs, &req->node);
	*ret_req = req;
	return 0;

error:
	arsdk_ephemeris_destroy_req_upload(req);
	return res;
}

int arsdk_ephemeris_req_upload_cancel(struct arsdk_ephemeris_req_upload *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	if (!req->is_running)
		return -EBUSY;

	req->is_running = 0;

	if (req->md5.ftp_put_req != NULL)
		arsdk_ftp_req_put_cancel(req->md5.ftp_put_req);
	if (req->md5.ftp_get_req != NULL)
		arsdk_ftp_req_get_cancel(req->md5.ftp_get_req);
	if (req->eph.ftp_put_req != NULL)
		arsdk_ftp_req_put_cancel(req->eph.ftp_put_req);

	/* delete request */
	list_del(&req->node);
	arsdk_ephemeris_destroy_req_upload(req);

	return 0;
}

enum arsdk_device_type arsdk_ephemeris_req_upload_get_dev_type(
		const struct arsdk_ephemeris_req_upload *req)
{
	return req == NULL ? ARSDK_DEVICE_TYPE_UNKNOWN : req->dev_type;
}

const char *arsdk_ephemeris_req_upload_get_file_path(
		const struct arsdk_ephemeris_req_upload *req)
{
	return (req != NULL) ? req->local_filepath : NULL;
}
