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
#include "arsdk_updater_itf_priv.h"
#include "updater/arsdk_updater_transport.h"
#include "updater/arsdk_updater_transport_priv.h"
#include "arsdk_updater_transport_ftp.h"

#define ARSDK_UPDATER_TRANSPORT_TAG             "ftp"

struct arsdk_updater_transport_ftp {
	struct arsdk_updater_transport          *parent;
	struct arsdk_ftp_itf                    *ftp;
	struct list_node                        reqs;
};

struct arsdk_updater_ftp_req_upload {
	struct arsdk_updater_req_upload         *parent;
	struct arsdk_updater_transport_ftp      *tsprt;
	uint8_t                                 is_aborted;
	enum arsdk_device_type                  dev_type;
	struct list_node                        node;

	struct arsdk_updater_req_upload_cbs     cbs;
	size_t                                  total_size;
	struct {
		struct arsdk_ftp_req_put        *ftp_put_req;
		double                          ulsize;
	} md5;
	struct {
		struct arsdk_ftp_req_put        *ftp_put_req;
		const char                      *file_name;
		char                            *remote_tmp_path;
		double                          ulsize;
	} fw;
	struct arsdk_ftp_req_rename             *ftp_rename_req;
	enum arsdk_updater_req_status           status;
	int                                     error;
};

static int stop_cb(struct arsdk_updater_transport *base)
{
	struct arsdk_updater_transport_ftp *tsprt =
			arsdk_updater_transport_get_child(base);

	return arsdk_updater_transport_ftp_stop(tsprt);
}

static int cancel_all_cb(struct arsdk_updater_transport *base)
{
	struct arsdk_updater_transport_ftp *tsprt =
			arsdk_updater_transport_get_child(base);

	return arsdk_updater_transport_ftp_cancel_all(tsprt);
}

static void arsdk_updater_req_upload_destroy(
	struct arsdk_updater_ftp_req_upload *req_upload)
{
	ARSDK_RETURN_IF_FAILED(req_upload != NULL, -EINVAL);

	if ((req_upload->md5.ftp_put_req != NULL) ||
	    (req_upload->fw.ftp_put_req != NULL))
		ARSDK_LOGW("request %p still pending", req_upload);

	arsdk_updater_destroy_req_upload(req_upload->parent);

	free(req_upload->fw.remote_tmp_path);
	free(req_upload);
}

static int create_req_upload_cb(struct arsdk_updater_transport *base,
			const char *fw_filepath,
			enum arsdk_device_type dev_type,
			const struct arsdk_updater_req_upload_cbs *cbs,
			struct arsdk_updater_req_upload **ret_req)
{
	int res = 0;
	struct arsdk_updater_ftp_req_upload *req = NULL;
	struct arsdk_updater_transport_ftp *tsprt =
			arsdk_updater_transport_get_child(base);

	res = arsdk_updater_transport_ftp_create_req_upload(tsprt,
			fw_filepath,
			dev_type,
			cbs,
			&req);
	if (res < 0)
		return res;

	res = arsdk_updater_new_req_upload(base, req, cbs, dev_type,
			&req->parent);
	if (res < 0) {
		arsdk_updater_req_upload_destroy(req);
		return res;
	}

	*ret_req = req->parent;
	return 0;
}

static int cancel_req_upload_cb(struct arsdk_updater_transport *base,
		struct arsdk_updater_req_upload *req)
{
	struct arsdk_updater_ftp_req_upload *req_ftp =
			arsdk_updater_req_upload_child(req);

	return arsdk_updater_ftp_req_upload_cancel(req_ftp);
}

static const struct arsdk_updater_transport_ops s_arsdk_updater_ops = {
	.stop = &stop_cb,
	.cancel_all = &cancel_all_cb,
	.create_req_upload = &create_req_upload_cb,
	.cancel_req_upload = &cancel_req_upload_cb,
};

int arsdk_updater_transport_ftp_new(struct arsdk_updater_itf *itf,
		struct arsdk_ftp_itf *ftp,
		struct arsdk_updater_transport_ftp **ret_obj)
{
	int res = 0;
	struct arsdk_updater_transport_ftp *self = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(ftp != NULL, -EINVAL);

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Initialize structure (make sure socket fds are setup before handling
	 * errors) */
	self->ftp = ftp;
	list_init(&self->reqs);

	/* Setup base structure */
	res = arsdk_updater_transport_new(self, ARSDK_UPDATER_TRANSPORT_TAG,
			&s_arsdk_updater_ops, itf, &self->parent);
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

static int arsdk_updater_req_abort(
		struct arsdk_updater_ftp_req_upload *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	req->is_aborted = 1;
	return arsdk_updater_ftp_req_upload_cancel(req);
}

static int arsdk_updater_transport_ftp_abort_all(
		struct arsdk_updater_transport_ftp *tsprt)
{
	struct arsdk_updater_ftp_req_upload *req = NULL;
	struct arsdk_updater_ftp_req_upload *req_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(tsprt != NULL, -EINVAL);

	list_walk_entry_forward_safe(&tsprt->reqs, req, req_tmp, node) {
		arsdk_updater_req_abort(req);
	}

	return 0;
}

int arsdk_updater_transport_ftp_stop(struct arsdk_updater_transport_ftp *tsprt)
{
	ARSDK_RETURN_ERR_IF_FAILED(tsprt != NULL, -EINVAL);

	arsdk_updater_transport_ftp_abort_all(tsprt);

	return 0;
}

int arsdk_updater_transport_ftp_cancel_all(
		struct arsdk_updater_transport_ftp *tsprt)
{
	struct arsdk_updater_ftp_req_upload *req = NULL;
	struct arsdk_updater_ftp_req_upload *req_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(tsprt != NULL, -EINVAL);

	list_walk_entry_forward_safe(&tsprt->reqs, req, req_tmp, node) {
		arsdk_updater_ftp_req_upload_cancel(req);
	}

	return 0;
}

int arsdk_updater_transport_ftp_destroy(
		struct arsdk_updater_transport_ftp *tsprt)
{
	ARSDK_RETURN_ERR_IF_FAILED(tsprt != NULL, -EINVAL);

	arsdk_updater_transport_ftp_stop(tsprt);

	free(tsprt);
	return 0;
}

int arsdk_updater_ftp_req_upload_cancel(
		struct arsdk_updater_ftp_req_upload *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	if (req->md5.ftp_put_req != NULL)
		arsdk_ftp_req_put_cancel(req->md5.ftp_put_req);
	if (req->fw.ftp_put_req != NULL)
		arsdk_ftp_req_put_cancel(req->fw.ftp_put_req);
	if (req->ftp_rename_req != NULL)
		arsdk_ftp_req_rename_cancel(req->ftp_rename_req);

	return 0;
}

static enum arsdk_updater_req_status ftp_to_updater_status(
		enum arsdk_ftp_req_status ftp_status)
{
	switch (ftp_status) {
	case ARSDK_FTP_REQ_STATUS_OK:
		return ARSDK_UPDATER_REQ_STATUS_OK;
	case ARSDK_FTP_REQ_STATUS_CANCELED:
		return ARSDK_UPDATER_REQ_STATUS_CANCELED;
	case ARSDK_FTP_REQ_STATUS_FAILED:
		return ARSDK_UPDATER_REQ_STATUS_FAILED;
	case ARSDK_FTP_REQ_STATUS_ABORTED:
		return ARSDK_UPDATER_REQ_STATUS_ABORTED;
	default:
		ARSDK_LOGE("ftp status not known. %d", ftp_status);
		return ARSDK_UPDATER_REQ_STATUS_FAILED;
	}
}

static void update_upload_complete(
		struct arsdk_updater_ftp_req_upload *req_upload)
{
	struct arsdk_updater_itf *itf = NULL;

	ARSDK_RETURN_IF_FAILED(req_upload != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(req_upload->tsprt != NULL, -EINVAL);

	itf = arsdk_updater_transport_get_itf(req_upload->tsprt->parent);

	ARSDK_LOGI("[%s] End of firmware upload with status : %d",
			ARSDK_UPDATER_TRANSPORT_TAG,
			req_upload->status);

	(*req_upload->cbs.complete)(
			itf,
			req_upload->parent,
			req_upload->status,
			req_upload->error,
			req_upload->cbs.userdata);

	/* cleanup */
	list_del(&req_upload->node);
	arsdk_updater_req_upload_destroy(req_upload);
}

static void update_rename_complete_cb(struct arsdk_ftp_itf *itf,
		struct arsdk_ftp_req_rename *req,
		enum arsdk_ftp_req_status status,
		int error,
		void *userdata)
{
	struct arsdk_updater_ftp_req_upload *req_upload = userdata;

	ARSDK_RETURN_IF_FAILED(req_upload != NULL, -EINVAL);

	req_upload->status = ftp_to_updater_status(status);
	req_upload->error = error;

	update_upload_complete(req_upload);
}

static void update_put_progress_cb(struct arsdk_ftp_itf *itf,
		struct arsdk_ftp_req_put *req,
		float percent,
		void *userdata)
{
	struct arsdk_updater_itf *updater_itf = NULL;
	struct arsdk_updater_ftp_req_upload *req_upload = userdata;
	double ulsize = 0.0f;
	float update_percent = 0.0f;

	ARSDK_RETURN_IF_FAILED(req_upload != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(req_upload->tsprt != NULL, -EINVAL);

	updater_itf = arsdk_updater_transport_get_itf(
			req_upload->tsprt->parent);

	if (req == req_upload->md5.ftp_put_req)
		req_upload->md5.ulsize = arsdk_ftp_req_put_get_ulsize(
				req_upload->md5.ftp_put_req);
	else if (req == req_upload->fw.ftp_put_req)
		req_upload->fw.ulsize = arsdk_ftp_req_put_get_ulsize(
				req_upload->fw.ftp_put_req);

	ulsize = req_upload->md5.ulsize + req_upload->fw.ulsize;
	update_percent = (ulsize * 100) / req_upload->total_size;

	(*req_upload->cbs.progress)(updater_itf,
			req_upload->parent, update_percent,
			req_upload->cbs.userdata);
}

static void update_put_complete_cb(struct arsdk_ftp_itf *itf,
			struct arsdk_ftp_req_put *req,
			enum arsdk_ftp_req_status status,
			int error,
			void *userdata)
{
	int res = 0;
	struct arsdk_ftp_req_rename_cbs ftp_rename_cbs;
	struct arsdk_updater_ftp_req_upload *req_upload = userdata;

	ARSDK_RETURN_IF_FAILED(req_upload != NULL, -EINVAL);

	if (req_upload->status != ARSDK_UPDATER_REQ_STATUS_OK)
		goto end;

	if (req_upload->is_aborted) {
		req_upload->status = ARSDK_UPDATER_REQ_STATUS_ABORTED;
		goto end;
	}

	if (status != ARSDK_FTP_REQ_STATUS_OK) {
		req_upload->status = ftp_to_updater_status(status);
		req_upload->error = error;
		arsdk_updater_ftp_req_upload_cancel(req_upload);
	}

end:
	if (req == req_upload->md5.ftp_put_req)
		req_upload->md5.ftp_put_req = NULL;
	else if (req == req_upload->fw.ftp_put_req)
		req_upload->fw.ftp_put_req = NULL;

	if ((req_upload->md5.ftp_put_req != NULL) ||
	    (req_upload->fw.ftp_put_req != NULL))
		return;

	if (req_upload->status != ARSDK_UPDATER_REQ_STATUS_OK) {
		update_upload_complete(req_upload);
		return;
	}

	/* rename remote file */
	ftp_rename_cbs.userdata = req_upload;
	ftp_rename_cbs.complete = &update_rename_complete_cb;

	res = arsdk_ftp_itf_create_req_rename(req_upload->tsprt->ftp,
			&ftp_rename_cbs, req_upload->dev_type,
			ARSDK_FTP_SRV_TYPE_UPDATE,
			req_upload->fw.remote_tmp_path,
			req_upload->fw.file_name,
			&req_upload->ftp_rename_req);
	if (res < 0) {
		req_upload->status = ARSDK_UPDATER_REQ_STATUS_FAILED;
		req_upload->error = res;
		update_upload_complete(req_upload);
		return;
	}
}

static const char *get_file_name(enum arsdk_device_type dev_type)
{
	static const struct {
		enum arsdk_device_type type;
		const char *folder;
	} dev_flds[] = {
		{ARSDK_DEVICE_TYPE_BEBOP, "bebopdrone_update.plf"},
		{ARSDK_DEVICE_TYPE_BEBOP_2, "bebop2_update.plf"},
		{ARSDK_DEVICE_TYPE_PAROS, "paros_update.plf"},
		{ARSDK_DEVICE_TYPE_CHIMERA, "chimera_update.plf"},
		{ARSDK_DEVICE_TYPE_SKYCTRL, "nap_update.plf"},
		{ARSDK_DEVICE_TYPE_SKYCTRL_2, "mpp_update.plf"},
		{ARSDK_DEVICE_TYPE_SKYCTRL_2P, "mpp2_update.tar.gz"},
		{ARSDK_DEVICE_TYPE_SKYCTRL_3, "mpp3_update.tar.gz"},
		{ARSDK_DEVICE_TYPE_SKYCTRL_UA, "mppua_update.tar.gz"},
		{ARSDK_DEVICE_TYPE_JS, "jumpingsumo_update.plf"},
		{ARSDK_DEVICE_TYPE_JS_EVO_LIGHT, "jumpingsumo_update.plf"},
		{ARSDK_DEVICE_TYPE_JS_EVO_RACE, "jumpingsumo_update.plf"},
		{ARSDK_DEVICE_TYPE_RS, "rollingspider_update.plf"},
		{ARSDK_DEVICE_TYPE_RS_EVO_LIGHT, "rollingspider_update.plf"},
		{ARSDK_DEVICE_TYPE_RS_EVO_BRICK, "rollingspider_update.plf"},
		{ARSDK_DEVICE_TYPE_RS_EVO_HYDROFOIL,
				"rollingspider_update.plf"},
		{ARSDK_DEVICE_TYPE_POWERUP, "powerup_update.plf"},
		{ARSDK_DEVICE_TYPE_EVINRUDE, "disco_update.plf"},
		{ARSDK_DEVICE_TYPE_RS3, "rollingspider_update.plf"},
		{ARSDK_DEVICE_TYPE_WINGX, "rollingspider_update.plf"},
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

int arsdk_updater_transport_ftp_create_req_upload(
		struct arsdk_updater_transport_ftp *tsprt,
		const char *fw_filepath,
		enum arsdk_device_type dev_type,
		const struct arsdk_updater_req_upload_cbs *cbs,
		struct arsdk_updater_ftp_req_upload **ret_req)
{
	int res = 0;
	struct arsdk_updater_ftp_req_upload *req_upload = NULL;
	struct arsdk_ftp_req_put_cbs ftp_put_cbs;
	char remote_update_path[500] = "";
	struct arsdk_updater_fw_info fw_info;
	char md5_str[2 * ARSDK_MD5_LENGTH + 1];
	size_t md5_str_len = 0;
	struct pomp_buffer *md5_buff = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_req != NULL, -EINVAL);
	*ret_req = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(fw_filepath != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(tsprt != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->complete != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->progress != NULL, -EINVAL);

	/* Get firmware info */
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

	ftp_put_cbs.userdata = req_upload;
	ftp_put_cbs.complete = &update_put_complete_cb;
	ftp_put_cbs.progress = &update_put_progress_cb;

	/* create md5 buff*/
	arsdk_md5_to_str(fw_info.md5, md5_str, sizeof(md5_str));
	md5_str_len = strlen(md5_str);
	md5_buff = pomp_buffer_new_with_data(md5_str, md5_str_len);
	if (md5_buff == NULL) {
		res = -ENOMEM;
		goto error;
	}

	/* upload md5 file */
	res =  arsdk_ftp_itf_create_req_put_buff(tsprt->ftp, &ftp_put_cbs,
			dev_type, ARSDK_FTP_SRV_TYPE_UPDATE, "/md5_check.md5",
			md5_buff, 0, &req_upload->md5.ftp_put_req);
	pomp_buffer_unref(md5_buff);
	if (res < 0)
		goto error;

	req_upload->total_size += arsdk_ftp_req_put_get_total_size(
			req_upload->md5.ftp_put_req);

	/* upload updater file */
	req_upload->fw.file_name = get_file_name(dev_type);
	if (req_upload->fw.file_name == NULL) {
		res = -ENOENT;
		goto error;
	}
	snprintf(remote_update_path, sizeof(remote_update_path),
			"/%s.tmp", req_upload->fw.file_name);

	req_upload->fw.remote_tmp_path = xstrdup(remote_update_path);
	res =  arsdk_ftp_itf_create_req_put(tsprt->ftp, &ftp_put_cbs, dev_type,
			ARSDK_FTP_SRV_TYPE_UPDATE, remote_update_path,
			fw_filepath, 0, &req_upload->fw.ftp_put_req);
	if (res < 0)
		goto error;

	req_upload->total_size += arsdk_ftp_req_put_get_total_size(
			req_upload->fw.ftp_put_req);

	ARSDK_LOGI("[%s] Start to upload firmware :\n"
			"\t- product:\t0x%04x\n"
			"\t- version:\t%s\n"
			"\t- size:\t%zu",
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

struct arsdk_updater_transport *arsdk_updater_transport_ftp_get_parent(
		struct arsdk_updater_transport_ftp *self)
{
	return self == NULL ? NULL : self->parent;
}
