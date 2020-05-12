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

#define _GNU_SOURCE
#include "arsdkctrl_priv.h"
#include "arsdkctrl_default_log.h"
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ftw.h>

#include "arsdk_pud_itf_priv.h"

#define ARSDK_PUD_DIR_FORMAT "/internal_000/%s/academy/"
#define ARSDK_PUD_EXT ".pud"
#define ARSDK_PUD_EXT_LEN 4
#define ARSDK_PUD_TMP_EXT "tmp"

/** */
struct arsdk_pud_itf {
	struct arsdk_device_info                *dev_info;
	struct arsdk_ftp_itf                    *ftp_itf;
	struct list_node                        reqs;
};

struct arsdk_pud_req {
	struct arsdk_pud_itf                    *itf;
	struct arsdk_pud_req_cbs                cbs;
	enum arsdk_device_type                  dev_type;
	char                                    *remote_path;
	char                                    *local_path;
	struct arsdk_ftp_req_list               *ftp_list_req;
	struct arsdk_ftp_file_list              *dir_list;

	size_t                                  total;
	size_t                                  count;
	struct arsdk_ftp_file                   *curr_pud;
	struct simple_req                       *curr_req;
	int                                     is_running;
	int                                     is_aborted;
	struct list_node                        node;
};

/** */
struct simple_req {
	struct arsdk_pud_req                    *req;
	char                                    *name;
	char                                    *remote_path;
	char                                    *local_path;
	char                                    *local_path_tmp;
	struct arsdk_ftp_req_get                *get_req;
	struct arsdk_ftp_req_delete             *del_req;
	enum arsdk_pud_req_status               status;
	int                                     error;
};

/* forward declaration */
static int req_start_next(struct arsdk_pud_req *req);

int arsdk_pud_itf_new(struct arsdk_device_info *dev_info,
		struct arsdk_ftp_itf *ftp_itf,
		struct arsdk_pud_itf **ret_itf)
{
	struct arsdk_pud_itf *itf = NULL;

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
	itf->ftp_itf = ftp_itf;
	list_init(&itf->reqs);

	*ret_itf = itf;
	return 0;
}

int arsdk_pud_itf_destroy(struct arsdk_pud_itf *itf)
{
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	arsdk_pud_itf_stop(itf);

	free(itf);
	return 0;
}

static int arsdk_pud_req_delete(struct arsdk_pud_req *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	if (req->dir_list != NULL)
		arsdk_ftp_file_list_unref(req->dir_list);

	free(req->remote_path);
	free(req->local_path);
	free(req);

	return 0;
}

static int arsdk_pud_req_abort(struct arsdk_pud_req *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	req->is_aborted = 1;
	return arsdk_pud_req_cancel(req);
}

static int arsdk_pud_itf_abort_all(struct arsdk_pud_itf *itf)
{
	struct arsdk_pud_req *req = NULL;
	struct arsdk_pud_req *req_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	list_walk_entry_forward_safe(&itf->reqs, req, req_tmp, node) {
		arsdk_pud_req_abort(req);
	}

	return 0;
}

int arsdk_pud_itf_stop(struct arsdk_pud_itf *itf)
{
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	arsdk_pud_itf_abort_all(itf);

	return 0;
}

static enum arsdk_pud_req_status ftp_to_pud_status(
		enum arsdk_ftp_req_status ftp_status)
{
	switch (ftp_status) {
	case ARSDK_FTP_REQ_STATUS_OK:
		return ARSDK_PUD_REQ_STATUS_OK;
	case ARSDK_FTP_REQ_STATUS_FAILED:
		return ARSDK_PUD_REQ_STATUS_FAILED;
	case ARSDK_FTP_REQ_STATUS_CANCELED:
		return ARSDK_PUD_REQ_STATUS_CANCELED;
	case ARSDK_FTP_REQ_STATUS_ABORTED:
		return ARSDK_PUD_REQ_STATUS_ABORTED;
	default:
		ARSDK_LOGE("ftp status not known. %d", ftp_status);
		return ARSDK_PUD_REQ_STATUS_FAILED;
	}
}

static void simple_req_delete(struct simple_req *req)
{
	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);

	free(req->name);
	free(req->local_path);
	free(req->local_path_tmp);
	free(req->remote_path);
	free(req);
}

/**
 * end of a pud itf run.
 */
static void req_done(struct arsdk_pud_req *req)
{
	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);

	if (req->is_running) {
		(*req->cbs.complete)(req->itf, req, ARSDK_PUD_REQ_STATUS_OK,
				0, req->cbs.userdata);
		list_del(&req->node);
		arsdk_pud_req_delete(req);
	} else {
		(*req->cbs.complete)(req->itf, req,
				ARSDK_PUD_REQ_STATUS_ABORTED, 0,
				req->cbs.userdata);
	}
}

/**
 * end of a pud request.
 */
static void curr_req_done(struct arsdk_pud_req *req,
		enum arsdk_pud_req_status status, int error)
{
	int res = 0;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(req->curr_req != NULL, -EINVAL);

	/* callback */
	(*req->cbs.progress)(req->itf, req, req->curr_req->local_path,
			req->count,
			req->total,
			status,
			req->cbs.userdata);

	if (req->curr_req->status != ARSDK_PUD_REQ_STATUS_CANCELED &&
	    req->curr_req->status != ARSDK_PUD_REQ_STATUS_ABORTED) {
		/* delete current request */
		simple_req_delete(req->curr_req);
		req->curr_req = NULL;
	}

	if (!req->is_running) {
		/* pud is stopped */
		req_done(req);
		return;
	}

	/* start to download the next pud */
	res = req_start_next(req);
	if (res < 0) {
		/* failed to start the next request */
		req_done(req);
	}
}

static void ftp_del_file_complete(struct arsdk_ftp_itf *ftp_itf,
			struct arsdk_ftp_req_delete *ftp_req,
			enum arsdk_ftp_req_status status,
			int error,
			void *userdata)
{
	struct arsdk_pud_req *req = userdata;
	struct simple_req *curr_req = NULL;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(req->curr_req != NULL, -EINVAL);
	curr_req = req->curr_req;

	if (curr_req->status == ARSDK_PUD_REQ_STATUS_OK &&
	    status != ARSDK_FTP_REQ_STATUS_OK) {
		curr_req->status = ftp_to_pud_status(status);
		curr_req->error = error;
	}

	curr_req->del_req = NULL;

	/* current request done */
	curr_req_done(req, curr_req->status, curr_req->error);
}

static int start_ftp_del_file(struct arsdk_pud_req *req,
		const char *remote_path)
{
	int res = 0;
	struct simple_req *curr_req = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->curr_req != NULL, -EINVAL);
	curr_req = req->curr_req;

	struct arsdk_ftp_req_delete_cbs ftp_cbs = {
		.userdata = req,
		.complete = &ftp_del_file_complete,
	};

	res = arsdk_ftp_itf_create_req_delete(req->itf->ftp_itf,
			&ftp_cbs, req->dev_type,
			ARSDK_FTP_SRV_TYPE_MEDIA, remote_path,
			&curr_req->del_req);
	if (res < 0)
		return res;

	return 0;
}

static void ftp_get_complete_cb(struct arsdk_ftp_itf *ftp_itf,
			struct arsdk_ftp_req_get *ftp_req,
			enum arsdk_ftp_req_status ftp_status,
			int error,
			void *userdata)
{
	struct arsdk_pud_req *req = userdata;
	int res = 0;
	struct simple_req *curr_req = NULL;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(req->curr_req != NULL, -EINVAL);
	curr_req = req->curr_req;

	if (curr_req->status == ARSDK_PUD_REQ_STATUS_OK &&
	    ftp_status != ARSDK_FTP_REQ_STATUS_OK) {
		curr_req->status = ftp_to_pud_status(ftp_status);
		curr_req->error = error;
		res = error;
	}

	/* no ftp get request pending */
	curr_req->get_req = NULL;

	if (curr_req->status != ARSDK_PUD_REQ_STATUS_OK) {
		curr_req_done(req, curr_req->status, curr_req->error);
		return;
	}

	/* rename local file */
	res = rename(curr_req->local_path_tmp, curr_req->local_path);
	if (res < 0) {
		ARSDK_LOG_ERRNO("rename failed", errno);
		curr_req_done(req, ARSDK_PUD_REQ_STATUS_FAILED, -errno);
		return;
	}

	/* delete pud file */
	res = start_ftp_del_file(req, curr_req->remote_path);
	if (res < 0)
		curr_req_done(req, ARSDK_PUD_REQ_STATUS_FAILED, res);
}

static void ftp_get_progress_cb(struct arsdk_ftp_itf *itf,
			struct arsdk_ftp_req_get *req,
			float percent,
			void *userdata)
{
	/* do nothing */
}

static int simple_req_new(struct arsdk_pud_req *req,
		const char *path,
		const char *name,
		struct simple_req **ret_req)
{
	int64_t now = 0;
	int res = 0;
	struct simple_req *simple_req = NULL;
	const struct arsdk_ftp_req_get_cbs ftp_cbs = {
		.userdata = req,
		.complete = &ftp_get_complete_cb,
		.progress = &ftp_get_progress_cb,
	};

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->itf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->itf->dev_info != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->local_path != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(path != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(name != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_req != NULL, -EINVAL);

	/* Allocate structure */
	simple_req = calloc(1, sizeof(*simple_req));
	if (simple_req == NULL)
		return -ENOMEM;

	simple_req->req = req;
	simple_req->name = xstrdup(name);
	if (simple_req->name == NULL) {
		res = -ENOMEM;
		goto error;
	}

	res = asprintf(&simple_req->remote_path, "%s/%s", path, name);
	if (res < 0) {
		res = -ENOMEM;
		goto error;
	}

	now = (int64_t)time(NULL);
	res = asprintf(&simple_req->local_path, "%s%s_%04x_%"PRIi64"_%s",
			req->local_path,
			req->itf->dev_info->id,
			req->dev_type,
			now,
			name);
	if (res < 0) {
		res = -ENOMEM;
		goto error;
	}

	res = asprintf(&simple_req->local_path_tmp, "%.*s_%s",
			(int)(strlen(simple_req->local_path)-1),
			simple_req->local_path,
			ARSDK_PUD_TMP_EXT);
	if (res < 0) {
		res = -ENOMEM;
		goto error;
	}

	res = arsdk_ftp_itf_create_req_get(req->itf->ftp_itf, &ftp_cbs,
			req->dev_type,
			ARSDK_FTP_SRV_TYPE_MEDIA,
			simple_req->remote_path,
			simple_req->local_path_tmp,
			0,
			&simple_req->get_req);
	if (res < 0)
		goto error;

	*ret_req = simple_req;

	return 0;
error:
	simple_req_delete(simple_req);
	return res;
}

static int pud_req_cancel(struct simple_req *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->req != NULL, -EINVAL);

	req->status = req->req->is_aborted ? ARSDK_PUD_REQ_STATUS_ABORTED :
					     ARSDK_PUD_REQ_STATUS_CANCELED;

	if (req->get_req != NULL)
		arsdk_ftp_req_get_cancel(req->get_req);

	if (req->del_req != NULL)
		arsdk_ftp_req_delete_cancel(req->del_req);

	simple_req_delete(req);
	return 0;
}

static int clean_local_dir(struct arsdk_pud_req *req)
{
	int res = 0;
	size_t len = 0;
	DIR *dir = NULL;
	struct dirent *curr = NULL;
	struct dirent *next = NULL;
	char path[500];

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->itf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->itf->dev_info != NULL, -EINVAL);

	/* delete probable local file tmp */

	dir = opendir(req->local_path);
	if (dir == NULL) {
		ARSDK_LOG_ERRNO("opendir failed", errno);
		return -errno;
	}

	next = readdir(dir);
	while (next != NULL) {
		curr = next;
		next = readdir(dir);

		len = strlen(curr->d_name);
		if (len < strlen(req->itf->dev_info->id) +
			  strlen(ARSDK_PUD_TMP_EXT))
			continue;

		/* check if the file name start by the good device ID */
		res = strncmp(curr->d_name, req->itf->dev_info->id,
				strlen(req->itf->dev_info->id));
		if (res != 0)
			continue;

		/* check if the file name end by "_tmp" */
		res = strncmp(&curr->d_name[len - 4], "_"ARSDK_PUD_TMP_EXT,
				strlen("_"ARSDK_PUD_TMP_EXT));
		if (res != 0)
			continue;

		/* delete old local file tmp */
		res = snprintf(path, sizeof(path), "%s%s",
				req->local_path, curr->d_name);
		if (res > (int)(sizeof(path)-1)) {
			ARSDK_LOGW("path buffer to small.");
			continue;
		}

		res = remove(path);
		if (res != 0)
			ARSDK_LOGW("failed to remove %s", curr->d_name);
	}

	res = closedir(dir);
	if (res < 0) {
		ARSDK_LOG_ERRNO("closedir failed", errno);
		return -errno;
	}

	return 0;
}

static int is_pud_file(struct arsdk_ftp_file *file)
{
	const char *fname = NULL;
	size_t len = 0;

	ARSDK_RETURN_VAL_IF_FAILED(file != NULL, -EINVAL, 0);

	fname = arsdk_ftp_file_get_name(file);
	len = strlen(fname);
	return ((fname != NULL) && (len > ARSDK_PUD_EXT_LEN) &&
	    (strcmp(&fname[len - 4], ARSDK_PUD_EXT) == 0));
}

static int req_start_next(struct arsdk_pud_req *req)
{
	int res = 0;
	const char *name = NULL;
	int success = 0;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	if (req->curr_req != NULL)
		return -EBUSY;

	/* cleanup local directory */
	clean_local_dir(req);

	do {
		req->curr_pud = arsdk_ftp_file_list_next_file(
				req->dir_list,
				req->curr_pud);
		if (req->curr_pud == NULL) {
			/* no next pud */
			goto done;
		}

		/* check if it is a pud */
		if (!is_pud_file(req->curr_pud))
			continue;

		req->count++;
		name = arsdk_ftp_file_get_name(req->curr_pud);
		res = simple_req_new(req, req->remote_path, name,
				&req->curr_req);
		if (res < 0) {
			curr_req_done(req, ARSDK_PUD_REQ_STATUS_FAILED,
					res);
			continue;
		}

		success = 1;
	} while (!success);

	return 0;
done:
	req_done(req);
	return 0;
}

static void puds_dir_list_complete_cb(struct arsdk_ftp_itf *ftp_itf,
		struct arsdk_ftp_req_list *ftp_req,
		enum arsdk_ftp_req_status ftp_status,
		int error,
		void *userdata)
{
	struct arsdk_pud_req *req = userdata;
	int res = 0;
	struct arsdk_ftp_file *curr = NULL;
	struct arsdk_ftp_file *next = NULL;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);

	if (!req->is_running)
		goto done;

	if (ftp_status != ARSDK_FTP_REQ_STATUS_OK)
		goto done;

	req->dir_list = arsdk_ftp_req_list_get_result(ftp_req);
	if (req->dir_list == NULL)
		goto done;

	arsdk_ftp_file_list_ref(req->dir_list);
	/* count total of pud to download */
	next = arsdk_ftp_file_list_next_file(req->dir_list, curr);
	while (next != NULL) {
		curr = next;
		next = arsdk_ftp_file_list_next_file(req->dir_list, curr);

		if (is_pud_file(curr))
			req->total++;
	}

	req->ftp_list_req = NULL;

	/* start to download the first pud */
	/* or close and free the req if nothing is to do */
	res = req_start_next(req);
	if (res < 0)
		goto done;

	return;
done:
	req->ftp_list_req = NULL;
	req_done(req);
}

int arsdk_pud_itf_cancel_all(struct arsdk_pud_itf *itf)
{
	struct arsdk_pud_req *req = NULL;
	struct arsdk_pud_req *req_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	list_walk_entry_forward_safe(&itf->reqs, req, req_tmp, node) {
		arsdk_pud_req_cancel(req);
	}

	return 0;
}

static int similar_req_exists(struct arsdk_pud_itf *itf,
		enum arsdk_device_type dev_type)
{
	struct arsdk_pud_req *req = NULL;
	struct arsdk_pud_req *req_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	list_walk_entry_forward_safe(&itf->reqs, req, req_tmp, node) {
		if (req->dev_type == dev_type)
			return 1;
	}

	return 0;
}

int arsdk_pud_itf_create_req(struct arsdk_pud_itf *itf,
		const char *local_path,
		enum arsdk_device_type dev_type,
		const struct arsdk_pud_req_cbs *cbs,
		struct arsdk_pud_req **ret_req)
{
	struct arsdk_pud_req *req = NULL;
	int res = 0;
	struct arsdk_ftp_req_list_cbs ftp_cbs;
	const char *fld_name = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(local_path != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->progress != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->complete != NULL, -EINVAL);

	/* check if a similar request is pending */
	res = similar_req_exists(itf, dev_type);
	if (res)
		return -EBUSY;

	req = calloc(1, sizeof(*req));
	if (req == NULL)
		return -ENOMEM;

	req->cbs = *cbs;
	req->dev_type = dev_type;
	req->itf = itf;

	req->local_path = xstrdup(local_path);
	if (req->local_path == NULL) {
		res = -ENOMEM;
		goto error;
	}

	fld_name = arsdk_device_type_to_fld(dev_type);
	if (fld_name == NULL) {
		res = -ENOENT;
		goto error;
	}
	res = asprintf(&req->remote_path, ARSDK_PUD_DIR_FORMAT, fld_name);
	if (res < 0) {
		res = -ENOMEM;
		goto error;
	}

	memset(&ftp_cbs, 0, sizeof(ftp_cbs));
	ftp_cbs.userdata = req;
	ftp_cbs.complete = &puds_dir_list_complete_cb;
	res =  arsdk_ftp_itf_create_req_list(itf->ftp_itf, &ftp_cbs,
			req->dev_type,
			ARSDK_FTP_SRV_TYPE_MEDIA,
			req->remote_path,
			&req->ftp_list_req);
	if (res < 0)
		goto error;

	req->is_running = 1;

	list_add_after(&itf->reqs, &req->node);
	*ret_req = req;
	return 0;
error:
	arsdk_pud_req_delete(req);
	return res;
}

int arsdk_pud_req_cancel(struct arsdk_pud_req *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	if (!req->is_running)
		return -EBUSY;

	req->is_running = 0;

	/* cancel main list request */
	if (req->ftp_list_req != NULL)
		arsdk_ftp_req_list_cancel(req->ftp_list_req);

	if (req->curr_req != NULL) {
		pud_req_cancel(req->curr_req);
		req->curr_req = NULL;
	}

	/* delete request */
	list_del(&req->node);
	arsdk_pud_req_delete(req);

	return 0;
}

enum arsdk_device_type arsdk_pud_req_get_dev_type(
		const struct arsdk_pud_req *req)
{
	return req == NULL ? ARSDK_DEVICE_TYPE_UNKNOWN : req->dev_type;
}
