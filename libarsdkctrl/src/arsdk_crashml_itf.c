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

#include "arsdk_crashml_itf_priv.h"

#define ARSDK_CRASHML_DIR_PATH "/internal_000/Debug/crash_reports/"
#define ARSDK_CRASHML_TMP_EXT "tmp"
#define ARSDK_CRASHML_TGZ_EXT ".tar.gz"

/** */
struct arsdk_crashml_itf {
	struct arsdk_device_info                *dev_info;
	struct arsdk_ftp_itf                    *ftp_itf;
	struct list_node                        reqs;
};

struct arsdk_crashml_req {
	struct arsdk_crashml_itf                *itf;
	uint32_t                                types;
	struct arsdk_crashml_req_cbs            cbs;
	enum arsdk_device_type                  dev_type;
	char                                    *local_path;
	struct arsdk_ftp_req_list               *ftp_list_req;
	struct arsdk_ftp_file_list              *dir_list;
	size_t                                  total;
	size_t                                  count;
	struct arsdk_ftp_file                   *curr_dir;
	struct simple_req                       *curr_req;
	int                                     is_running;
	int                                     is_aborted;
	struct list_node                        node;
};

/** */
struct simple_req {
	struct arsdk_crashml_req                *req;
	enum arsdk_crashml_type                 type;
	char                                    *name;
	char                                    *remote_crashpath;
	char                                    *local_crashpath;
	char                                    *local_crashpath_tmp;
	struct arsdk_ftp_req_list               *ftp_list_req;
	struct arsdk_ftp_file_list              *file_list;
	size_t                                  file_count;
	struct {
		struct arsdk_ftp_req_get        **reqs;
		size_t                          count;
	} ftp_get;
	struct {
		struct arsdk_ftp_req_delete     **reqs;
		size_t                          count;
	} ftp_del;
	enum arsdk_crashml_req_status           status;
	int                                     error;
};

/* forward declaration */
static int req_start_next(struct arsdk_crashml_req *req);

int arsdk_crashml_itf_new(struct arsdk_device_info *dev_info,
		struct arsdk_ftp_itf *ftp_itf,
		struct arsdk_crashml_itf **ret_itf)
{
	struct arsdk_crashml_itf *itf = NULL;

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

int arsdk_crashml_itf_destroy(struct arsdk_crashml_itf *itf)
{
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	arsdk_crashml_itf_stop(itf);

	free(itf);
	return 0;
}

static int arsdk_crashml_req_delete(struct arsdk_crashml_req *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	if (req->dir_list != NULL)
		arsdk_ftp_file_list_unref(req->dir_list);

	free(req->local_path);
	free(req);

	return 0;
}

static int arsdk_crashml_req_abort(struct arsdk_crashml_req *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	req->is_aborted = 1;
	return arsdk_crashml_req_cancel(req);
}

static int arsdk_crashm_itf_abort_all(struct arsdk_crashml_itf *itf)
{
	struct arsdk_crashml_req *req = NULL;
	struct arsdk_crashml_req *req_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	list_walk_entry_forward_safe(&itf->reqs, req, req_tmp, node) {
		arsdk_crashml_req_abort(req);
	}

	return 0;
}

int arsdk_crashml_itf_stop(struct arsdk_crashml_itf *itf)
{
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	arsdk_crashm_itf_abort_all(itf);

	return 0;
}

static enum arsdk_crashml_req_status ftp_to_crashml_status(
		enum arsdk_ftp_req_status ftp_status)
{
	switch (ftp_status) {
	case ARSDK_FTP_REQ_STATUS_OK:
		return ARSDK_CRASHML_REQ_STATUS_OK;
	case ARSDK_FTP_REQ_STATUS_FAILED:
		return ARSDK_CRASHML_REQ_STATUS_FAILED;
	case ARSDK_FTP_REQ_STATUS_CANCELED:
		return ARSDK_CRASHML_REQ_STATUS_CANCELED;
	case ARSDK_FTP_REQ_STATUS_ABORTED:
		return ARSDK_CRASHML_REQ_STATUS_ABORTED;
	default:
		ARSDK_LOGE("ftp status not known. %d", ftp_status);
		return ARSDK_CRASHML_REQ_STATUS_FAILED;
	}
}

static void simple_req_delete(struct simple_req *req)
{
	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);

	if (req->file_list != NULL)
		arsdk_ftp_file_list_unref(req->file_list);

	free(req->name);
	free(req->local_crashpath);
	free(req->local_crashpath_tmp);
	free(req->remote_crashpath);
	free(req->ftp_get.reqs);
	free(req->ftp_del.reqs);
	free(req);
}

/**
 * end of a crashml itf run.
 */
static void req_done(struct arsdk_crashml_req *req)
{
	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);

	if (req->is_running) {
		(*req->cbs.complete)(req->itf, req, ARSDK_CRASHML_REQ_STATUS_OK,
				0, req->cbs.userdata);
		list_del(&req->node);
		arsdk_crashml_req_delete(req);
	} else {
		(*req->cbs.complete)(req->itf, req,
				ARSDK_CRASHML_REQ_STATUS_ABORTED, 0,
				req->cbs.userdata);
	}
}

/**
 * end of a crashml request.
 */
static void curr_req_done(struct arsdk_crashml_req *req,
		enum arsdk_crashml_req_status status, int error)
{
	int res = 0;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(req->curr_req != NULL, -EINVAL);

	/* callback */
	(*req->cbs.progress)(req->itf, req, req->curr_req->local_crashpath,
			req->count,
			req->total,
			status,
			req->cbs.userdata);

	if (req->curr_req->status != ARSDK_CRASHML_REQ_STATUS_CANCELED &&
	    req->curr_req->status != ARSDK_CRASHML_REQ_STATUS_ABORTED) {
		/* delete current request */
		simple_req_delete(req->curr_req);
		req->curr_req = NULL;
	}

	if (!req->is_running) {
		/* crashml is stopped */
		req_done(req);
		return;
	}

	/* start to download the next crashml */
	res = req_start_next(req);
	if (res < 0) {
		/* failed to start the next request */
		req_done(req);
	}
}

static void ftp_del_dir_complete(struct arsdk_ftp_itf *ftp_itf,
			struct arsdk_ftp_req_delete *ftp_req,
			enum arsdk_ftp_req_status status,
			int error,
			void *userdata)
{
	struct arsdk_crashml_req *req = userdata;
	struct simple_req *curr_req = NULL;
	size_t i = 0;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(req->curr_req != NULL, -EINVAL);
	curr_req = req->curr_req;

	if (curr_req->status == ARSDK_CRASHML_REQ_STATUS_OK &&
	    status != ARSDK_FTP_REQ_STATUS_OK) {
		curr_req->status = ftp_to_crashml_status(status);
		curr_req->error = error;
	}

	/* remove ftp request */
	for (i = 0; i < curr_req->file_count; i++) {
		if (curr_req->ftp_del.reqs[i] == ftp_req) {
			curr_req->ftp_del.reqs[i] = NULL;
			curr_req->ftp_del.count--;
			break; /* found */
		}
	}

	if (curr_req->ftp_del.count != 0) {
		ARSDK_LOGW("an unexpected ftp delete request is pending");
		return;
	}

	/* no ftp delete request pending */

	/* current request done */
	curr_req_done(req, curr_req->status, curr_req->error);
}

static int start_ftp_del_dir(struct arsdk_crashml_req *req)
{
	struct arsdk_ftp_req_delete_cbs ftp_cbs = {
		.userdata = req,
		.complete = &ftp_del_dir_complete,
	};
	int res = 0;
	struct simple_req *curr_req = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->itf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->curr_req != NULL, -EINVAL);
	curr_req = req->curr_req;

	res = arsdk_ftp_itf_create_req_delete(req->itf->ftp_itf, &ftp_cbs,
			req->dev_type, ARSDK_FTP_SRV_TYPE_MEDIA,
			curr_req->remote_crashpath,
			&curr_req->ftp_del.reqs[curr_req->ftp_del.count]);
	if (res < 0)
		return res;

	curr_req->ftp_del.count++;

	return 0;
}

static void ftp_del_file_complete(struct arsdk_ftp_itf *ftp_itf,
			struct arsdk_ftp_req_delete *ftp_req,
			enum arsdk_ftp_req_status ftp_status,
			int error,
			void *userdata)
{
	struct arsdk_crashml_req *req = userdata;
	struct simple_req *curr_req = NULL;
	int res = 0;
	size_t i = 0;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(req->curr_req != NULL, -EINVAL);
	curr_req = req->curr_req;

	if (curr_req->status == ARSDK_CRASHML_REQ_STATUS_OK &&
	    ftp_status != ARSDK_FTP_REQ_STATUS_OK) {
		curr_req->status = ftp_to_crashml_status(ftp_status);
		curr_req->error = error;
	}

	/* remove ftp request */
	for (i = 0; i < curr_req->file_count; i++) {
		if (curr_req->ftp_del.reqs[i] == ftp_req) {
			curr_req->ftp_del.reqs[i] = NULL;
			curr_req->ftp_del.count--;
			break; /* found */
		}
	}

	if (curr_req->ftp_del.count != 0)
		return;

	/* no ftp delete request pending */

	if (curr_req->status != ARSDK_CRASHML_REQ_STATUS_OK) {
		curr_req_done(req, curr_req->status, curr_req->error);
		return;
	}

	/* delete crashml directory */
	res = start_ftp_del_dir(req);
	if (res < 0)
		curr_req_done(req, ARSDK_CRASHML_REQ_STATUS_FAILED, res);
}

static int ftp_del_file(struct arsdk_crashml_req *req,
		struct arsdk_ftp_file *file)
{
	int res = 0;
	char fpath[500];
	const char *fname = NULL;
	struct simple_req *curr_req = NULL;
	struct arsdk_ftp_req_delete_cbs ftp_cbs = {
		.userdata = req,
		.complete = &ftp_del_file_complete,
	};

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->curr_req != NULL, -EINVAL);
	curr_req = req->curr_req;

	fname = arsdk_ftp_file_get_name(file);
	snprintf(fpath, sizeof(fpath), "%s/%s",
			curr_req->remote_crashpath, fname);
	res = arsdk_ftp_itf_create_req_delete(req->itf->ftp_itf,
			&ftp_cbs, req->dev_type,
			ARSDK_FTP_SRV_TYPE_MEDIA, fpath,
			&curr_req->ftp_del.reqs[curr_req->ftp_del.count]);
	if (res < 0)
		return res;

	curr_req->ftp_del.count++;

	return 0;
}

static int ftp_del_tgz(struct arsdk_crashml_req *req)
{
	int res = 0;
	struct simple_req *curr_req = NULL;
	struct arsdk_ftp_req_delete_cbs ftp_cbs = {
		.userdata = req,
		.complete = &ftp_del_file_complete,
	};

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->curr_req != NULL, -EINVAL);
	curr_req = req->curr_req;

	/* create ftp delete request array */
	curr_req->ftp_del.reqs = calloc(1,
			sizeof(*curr_req->ftp_del.reqs));
	if (curr_req->ftp_del.reqs == NULL)
		return -ENOMEM;

	res = arsdk_ftp_itf_create_req_delete(req->itf->ftp_itf,
			&ftp_cbs, req->dev_type,
			ARSDK_FTP_SRV_TYPE_MEDIA, curr_req->remote_crashpath,
			&curr_req->ftp_del.reqs[0]);
	if (res < 0)
		return res;

	curr_req->ftp_del.count++;

	return 0;
}

static int start_ftp_del_files(struct arsdk_crashml_req *req)
{
	int res = 0;
	struct arsdk_ftp_file *next = NULL;
	struct arsdk_ftp_file *curr = NULL;
	struct simple_req *curr_req = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->curr_req != NULL, -EINVAL);
	curr_req = req->curr_req;

	/* create ftp get request array */
	curr_req->ftp_del.reqs = calloc(curr_req->file_count,
			sizeof(*curr_req->ftp_del.reqs));
	if (curr_req->ftp_del.reqs == NULL)
		return -ENOMEM;

	/* start ftp delete requests */
	next = arsdk_ftp_file_list_next_file(curr_req->file_list, curr);
	while (next != NULL) {
		curr = next;
		next = arsdk_ftp_file_list_next_file(curr_req->file_list, curr);

		res = ftp_del_file(req, curr);
		if (res < 0)
			return res;
	}

	return 0;
}

static void ftp_get_complete_cb(struct arsdk_ftp_itf *ftp_itf,
			struct arsdk_ftp_req_get *ftp_req,
			enum arsdk_ftp_req_status ftp_status,
			int error,
			void *userdata)
{
	struct arsdk_crashml_req *req = userdata;
	size_t i = 0;
	int res = 0;
	struct simple_req *curr_req = NULL;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(req->curr_req != NULL, -EINVAL);
	curr_req = req->curr_req;

	if (curr_req->status == ARSDK_CRASHML_REQ_STATUS_OK &&
	    ftp_status != ARSDK_FTP_REQ_STATUS_OK) {
		curr_req->status = ftp_to_crashml_status(ftp_status);
		curr_req->error = error;
		res = error;
	}

	/* remove ftp request */
	for (i = 0; i < curr_req->file_count; i++) {
		if (curr_req->ftp_get.reqs[i] == ftp_req) {
			curr_req->ftp_get.reqs[i] = NULL;
			curr_req->ftp_get.count--;
			break; /* found */
		}
	}

	if (curr_req->ftp_get.count != 0)
		return;

	/* no ftp get request pending */

	if (curr_req->status != ARSDK_CRASHML_REQ_STATUS_OK) {
		curr_req_done(req, curr_req->status, curr_req->error);
		return;
	}

	/* rename local directory */
	res = rename(curr_req->local_crashpath_tmp, curr_req->local_crashpath);
	if (res < 0) {
		ARSDK_LOG_ERRNO("rename failed", errno);
		curr_req_done(req, ARSDK_CRASHML_REQ_STATUS_FAILED, -errno);
		return;
	}

	/* delete all crashml files */
	switch (curr_req->type) {
	case ARSDK_CRASHML_TYPE_DIR:
		res = start_ftp_del_files(req);
		break;
	case ARSDK_CRASHML_TYPE_TARGZ:
		res = ftp_del_tgz(req);
		break;
	default:
		res = -EPROTO;
		break;
	}
	if (res < 0)
		curr_req_done(req, ARSDK_CRASHML_REQ_STATUS_FAILED, res);
}

static void ftp_get_progress_cb(struct arsdk_ftp_itf *itf,
			struct arsdk_ftp_req_get *req,
			float percent,
			void *userdata)
{
	/* do nothing */
}

static int ftp_get_tgz(struct arsdk_crashml_req *req,
		struct simple_req *curr_req)
{
	int res = 0;
	const struct arsdk_ftp_req_get_cbs ftp_cbs = {
		.userdata = req,
		.complete = &ftp_get_complete_cb,
		.progress = &ftp_get_progress_cb,
	};

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(curr_req != NULL, -EINVAL);

	curr_req->file_count = 1;
	curr_req->ftp_get.reqs = calloc(1, sizeof(*curr_req->ftp_get.reqs));
	if (curr_req->ftp_get.reqs == NULL)
			return -ENOMEM;

	res = arsdk_ftp_itf_create_req_get(req->itf->ftp_itf, &ftp_cbs,
			req->dev_type,
			ARSDK_FTP_SRV_TYPE_MEDIA,
			curr_req->remote_crashpath,
			curr_req->local_crashpath_tmp,
			0,
			&curr_req->ftp_get.reqs[0]);
	if (res < 0)
		return res;

	curr_req->ftp_get.count++;
	return 0;
}

static int ftp_get_file(struct arsdk_crashml_req *req,
		struct arsdk_ftp_file *file)
{
	int res = 0;
	char remote_fpath[500];
	const char *fname = NULL;
	struct simple_req *curr_req = NULL;
	const struct arsdk_ftp_req_get_cbs ftp_cbs = {
		.userdata = req,
		.complete = &ftp_get_complete_cb,
		.progress = &ftp_get_progress_cb,
	};

	ARSDK_RETURN_ERR_IF_FAILED(file != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->curr_req != NULL, -EINVAL);
	curr_req = req->curr_req;

	fname = arsdk_ftp_file_get_name(file);
	snprintf(remote_fpath, sizeof(remote_fpath), "%s/%s",
			curr_req->remote_crashpath, fname);
	res = arsdk_ftp_itf_create_req_get(req->itf->ftp_itf, &ftp_cbs,
			req->dev_type,
			ARSDK_FTP_SRV_TYPE_MEDIA,
			remote_fpath,
			curr_req->local_crashpath_tmp,
			0,
			&curr_req->ftp_get.reqs[curr_req->ftp_get.count]);
	if (res < 0)
		return res;

	curr_req->ftp_get.count++;
	return 0;
}

static int start_ftp_get_files(struct arsdk_crashml_req *req)
{
	int res = 0;
	struct arsdk_ftp_file *next = NULL;
	struct arsdk_ftp_file *curr = NULL;
	struct simple_req *curr_req = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->curr_req != NULL, -EINVAL);
	curr_req = req->curr_req;

	/* create ftp get request array */
	curr_req->ftp_get.reqs = calloc(curr_req->file_count,
			sizeof(*curr_req->ftp_get.reqs));
	if (curr_req->ftp_get.reqs == NULL)
		return -ENOMEM;

	/* start ftp get requests */
	next = arsdk_ftp_file_list_next_file(curr_req->file_list, curr);
	while (next != NULL) {
		curr = next;
		next = arsdk_ftp_file_list_next_file(curr_req->file_list, curr);

		res = ftp_get_file(req, curr);
		if (res < 0)
			return res;
	}

	return 0;
}

static void ftp_single_list_complete_cb(struct arsdk_ftp_itf *ftp_itf,
		struct arsdk_ftp_req_list *ftp_req,
		enum arsdk_ftp_req_status ftp_status,
		int error,
		void *userdata)
{
	struct arsdk_crashml_req *req = userdata;
	struct simple_req *curr_req = NULL;
	int res = 0;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(req->curr_req != NULL, -EINVAL);
	curr_req = req->curr_req;

	if (curr_req->status != ARSDK_CRASHML_REQ_STATUS_OK)
		goto done;
	if (ftp_status != ARSDK_FTP_REQ_STATUS_OK) {
		curr_req->status = ftp_to_crashml_status(ftp_status);
		res = error;
		goto done;
	}

	/* get file list */
	curr_req->file_list = arsdk_ftp_req_list_get_result(ftp_req);
	if (curr_req->file_list == NULL) {
		curr_req->status = ARSDK_CRASHML_REQ_STATUS_FAILED;
		goto done;
	}
	arsdk_ftp_file_list_ref(curr_req->file_list);

	/* get number of files */
	curr_req->file_count = arsdk_ftp_file_list_get_count(
			curr_req->file_list);
	if (curr_req->file_count == 0) {
		/* crashml folder empty */
		curr_req->status = ARSDK_CRASHML_REQ_STATUS_FAILED;
		goto done;
	}

	/* check tmp directory existence */
	res = access(curr_req->local_crashpath_tmp, F_OK);
	if (res == 0) {
		ARSDK_LOGW("%s already exists.", curr_req->local_crashpath_tmp);
		res = -EEXIST;
		goto done;
	} else if (errno != ENOENT) {
		ARSDK_LOG_ERRNO("access failed", errno);
		res = -errno;
		curr_req->status = ARSDK_CRASHML_REQ_STATUS_FAILED;
		goto done;
	}

	/* directory not exist create it */
#ifdef _WIN32
	res = mkdir(curr_req->local_crashpath_tmp);
#else /* !_WIN32 */
	res = mkdir(curr_req->local_crashpath_tmp,
		    S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
#endif /* !_WIN32 */
	if (res < 0) {
		ARSDK_LOG_ERRNO("mkdir failed", errno);
		res = -errno;
		curr_req->status = ARSDK_CRASHML_REQ_STATUS_FAILED;
		goto done;
	}

	/* start ftp get requests */
	res = start_ftp_get_files(req);
	if (res < 0) {
		curr_req->status = ARSDK_CRASHML_REQ_STATUS_FAILED;
		goto done;
	}

	curr_req->ftp_list_req = NULL;
	return;
done:
	curr_req->ftp_list_req = NULL;
	curr_req_done(req, curr_req->status, res);
}

static int simple_req_new(struct arsdk_crashml_req *req,
		enum arsdk_crashml_type type,
		const char *crash_name,
		struct simple_req **ret_req)
{
	struct arsdk_ftp_req_list_cbs ftp_cbs = {
		.userdata = req,
		.complete = &ftp_single_list_complete_cb,
	};
	int64_t now = 0;
	int res = 0;
	struct simple_req *simple_req = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->itf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->itf->dev_info != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->local_path != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(crash_name != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_req != NULL, -EINVAL);

	/* Allocate structure */
	simple_req = calloc(1, sizeof(*simple_req));
	if (simple_req == NULL)
		return -ENOMEM;

	simple_req->req = req;
	simple_req->type = type;
	simple_req->name = xstrdup(crash_name);
	if (simple_req->name == NULL) {
		res = -ENOMEM;
		goto error;
	}

	res = asprintf(&simple_req->remote_crashpath, "%s%s%s",
			ARSDK_CRASHML_DIR_PATH, crash_name,
			(type == ARSDK_CRASHML_TYPE_DIR) ? "/" : "");
	if (res < 0) {
		res = -ENOMEM;
		goto error;
	}

	now = (int64_t)time(NULL);
	res = asprintf(&simple_req->local_crashpath, "%s%s_%04x_%"PRIi64"_%s%s",
			req->local_path,
			req->itf->dev_info->id,
			req->dev_type,
			now,
			crash_name,
			(type == ARSDK_CRASHML_TYPE_DIR) ? "/" : "");
	if (res < 0) {
		res = -ENOMEM;
		goto error;
	}

	switch (type) {
	case ARSDK_CRASHML_TYPE_DIR:
		res = asprintf(&simple_req->local_crashpath_tmp, "%.*s_%s/",
				(int)(strlen(simple_req->local_crashpath)-1),
				simple_req->local_crashpath,
				ARSDK_CRASHML_TMP_EXT);
		if (res < 0) {
			res = -ENOMEM;
			goto error;
		}

		/* list all file in the crash directory */
		res = arsdk_ftp_itf_create_req_list(req->itf->ftp_itf, &ftp_cbs,
				req->dev_type, ARSDK_FTP_SRV_TYPE_MEDIA,
				simple_req->remote_crashpath,
				&simple_req->ftp_list_req);
		if (res < 0)
			goto error;
		break;
	case ARSDK_CRASHML_TYPE_TARGZ:
		res = asprintf(&simple_req->local_crashpath_tmp, "%s_%s",
				simple_req->local_crashpath,
				ARSDK_CRASHML_TMP_EXT);
		if (res < 0) {
			res = -ENOMEM;
			goto error;
		}

		/* get the tar.gz */
		res = ftp_get_tgz(req, simple_req);
		if (res < 0)
			goto error;
		break;
	default:
		res = -EPROTO;
		goto error;
	}

	*ret_req = simple_req;

	return 0;
error:
	simple_req_delete(simple_req);
	return res;
}

static int simple_req_cancel(struct simple_req *req)
{
	size_t i = 0;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->req != NULL, -EINVAL);

	req->status = req->req->is_aborted ? ARSDK_CRASHML_REQ_STATUS_ABORTED :
					     ARSDK_CRASHML_REQ_STATUS_CANCELED;

	if (req->ftp_list_req != NULL)
		arsdk_ftp_req_list_cancel(req->ftp_list_req);

	if (req->ftp_get.reqs != NULL && req->ftp_get.count > 0) {
		for (i = 0; i < req->file_count; i++) {
			if (req->ftp_get.reqs[i] != NULL)
				arsdk_ftp_req_get_cancel(req->ftp_get.reqs[i]);
		}
	}

	if (req->ftp_del.reqs != NULL && req->ftp_del.count > 0) {
		for (i = 0; i < req->file_count; i++) {
			if (req->ftp_del.reqs[i] != NULL) {
				arsdk_ftp_req_delete_cancel(
						req->ftp_del.reqs[i]);
			}
		}
	}

	simple_req_delete(req);
	return 0;
}

static int unlink_cb(const char *fpath, const struct stat *sb, int typeflag,
		struct FTW *ftwbuf)
{
	int res = remove(fpath);
	if (res < 0) {
		ARSDK_LOG_ERRNO("remove failed", errno);
		return -errno;
	}

	return 0;
}

static int rmrf(const char *path)
{
	int res = 0;
	ARSDK_RETURN_ERR_IF_FAILED(path != NULL, -EINVAL);

	res = nftw(path, unlink_cb, 8, FTW_DEPTH | FTW_PHYS);
	if (res < 0) {
		ARSDK_LOG_ERRNO("nftw failed", errno);
		return -errno;
	}

	return res;
}

static int clean_local_dir(struct arsdk_crashml_req *req)
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

	/* delete probable local directory tmp */

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
			  strlen(ARSDK_CRASHML_TMP_EXT))
			continue;

		/* check if the directory name start by the good device ID */
		res = strncmp(curr->d_name, req->itf->dev_info->id,
				strlen(req->itf->dev_info->id));
		if (res != 0)
			continue;

		/* check if the directory name end by "_tmp" */
		res = strncmp(&curr->d_name[len - 4], "_"ARSDK_CRASHML_TMP_EXT,
				strlen("_"ARSDK_CRASHML_TMP_EXT));
		if (res != 0)
			continue;

		/* delete old local directory tmp */
		res = snprintf(path, sizeof(path), "%s%s",
				req->local_path, curr->d_name);
		if (res > (int)(sizeof(path)-1)) {
			ARSDK_LOGW("path buffer to small.");
			continue;
		}

		res = rmrf(path);
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

static int has_tgz_ext(const char *name)
{
	int res = 0;
	size_t len = 0;
	size_t ext_len = strlen(ARSDK_CRASHML_TGZ_EXT);

	if (name == NULL)
		return 0;

	/* check if the file name finished by .tar.gz */
	len = strlen(name);
	if (len < ext_len)
		return 0;

	res = strncmp(&name[len - ext_len], ARSDK_CRASHML_TGZ_EXT, ext_len);
	return (res == 0);
}

static int req_start_next(struct arsdk_crashml_req *req)
{
	int res = 0;
	const char *name = NULL;
	enum arsdk_ftp_file_type type = ARSDK_FTP_FILE_TYPE_UNKNOWN;
	int success = 0;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	if (req->curr_req != NULL)
		return -EBUSY;

	/* cleanup local directory */
	clean_local_dir(req);

	do {
		req->curr_dir = arsdk_ftp_file_list_next_file(
				req->dir_list,
				req->curr_dir);
		if (req->curr_dir == NULL) {
			/* no next crashml */
			goto done;
		}

		/* check if it is a crashml directory or tar.gz */
		type = arsdk_ftp_file_get_type(req->curr_dir);
		switch (type) {
		case ARSDK_FTP_FILE_TYPE_DIR:
			if (!(req->types & ARSDK_CRASHML_TYPE_DIR))
				continue;

			req->count++;
			name = arsdk_ftp_file_get_name(req->curr_dir);
			res = simple_req_new(req, ARSDK_CRASHML_TYPE_DIR, name,
					&req->curr_req);
			if (res < 0) {
				curr_req_done(req,
					      ARSDK_CRASHML_REQ_STATUS_FAILED,
					      res);
				continue;
			}
			success = 1;
			break;
		case ARSDK_FTP_FILE_TYPE_FILE:
			if (!(req->types & ARSDK_CRASHML_TYPE_TARGZ))
				continue;

			name = arsdk_ftp_file_get_name(req->curr_dir);
			if (!has_tgz_ext(name))
				continue;

			req->count++;
			res = simple_req_new(req, ARSDK_CRASHML_TYPE_TARGZ,
					name, &req->curr_req);
			if (res < 0) {
				curr_req_done(req,
					      ARSDK_CRASHML_REQ_STATUS_FAILED,
					      res);
				continue;
			}
			success = 1;
			break;
		default:
			break;
		}


	} while (!success);

	return 0;
done:
	req_done(req);
	return 0;
}

static void main_dir_list_complete_cb(struct arsdk_ftp_itf *ftp_itf,
		struct arsdk_ftp_req_list *ftp_req,
		enum arsdk_ftp_req_status ftp_status,
		int error,
		void *userdata)
{
	struct arsdk_crashml_req *req = userdata;
	int res = 0;
	struct arsdk_ftp_file *curr = NULL;
	struct arsdk_ftp_file *next = NULL;
	enum arsdk_ftp_file_type type = ARSDK_FTP_FILE_TYPE_UNKNOWN;
	const char *name = NULL;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);

	if (!req->is_running)
		goto done;

	if (ftp_status != ARSDK_FTP_REQ_STATUS_OK)
		goto done;

	req->dir_list = arsdk_ftp_req_list_get_result(ftp_req);
	if (req->dir_list == NULL)
		goto done;

	arsdk_ftp_file_list_ref(req->dir_list);
	/* count total of crashml to download */
	next = arsdk_ftp_file_list_next_file(req->dir_list, curr);
	while (next != NULL) {
		curr = next;
		next = arsdk_ftp_file_list_next_file(req->dir_list, curr);

		type = arsdk_ftp_file_get_type(curr);
		switch (type) {
		case ARSDK_FTP_FILE_TYPE_DIR:
			if (req->types & ARSDK_CRASHML_TYPE_DIR)
				req->total++;
			break;
		case ARSDK_FTP_FILE_TYPE_FILE:
			if (!(req->types & ARSDK_CRASHML_TYPE_TARGZ))
				continue;

			name = arsdk_ftp_file_get_name(curr);
			if (has_tgz_ext(name))
				req->total++;
			break;
		default:
			break;
		}
	}

	req->ftp_list_req = NULL;

	/* start to download the first crashml */
	/* or close and free the req if nothing is to do */
	res = req_start_next(req);
	if (res < 0)
		goto done;

	return;
done:
	req->ftp_list_req = NULL;
	req_done(req);
}

int arsdk_crashml_itf_cancel_all(struct arsdk_crashml_itf *itf)
{
	struct arsdk_crashml_req *req = NULL;
	struct arsdk_crashml_req *req_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	list_walk_entry_forward_safe(&itf->reqs, req, req_tmp, node) {
		arsdk_crashml_req_cancel(req);
	}

	return 0;
}

static int similar_req_exists(struct arsdk_crashml_itf *itf,
		enum arsdk_device_type dev_type)
{
	struct arsdk_crashml_req *req = NULL;
	struct arsdk_crashml_req *req_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	list_walk_entry_forward_safe(&itf->reqs, req, req_tmp, node) {
		if (req->dev_type == dev_type)
			return 1;
	}

	return 0;
}

int arsdk_crashml_itf_create_req(struct arsdk_crashml_itf *itf,
		const char *local_path,
		enum arsdk_device_type dev_type,
		const struct arsdk_crashml_req_cbs *cbs,
		uint32_t crashml_types,
		struct arsdk_crashml_req **ret_req)
{
	struct arsdk_crashml_req *req = NULL;
	int res = 0;
	struct arsdk_ftp_req_list_cbs ftp_cbs;

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
	req->types = crashml_types;

	req->local_path = xstrdup(local_path);
	if (req->local_path == NULL) {
		res = -ENOMEM;
		goto error;
	}

	memset(&ftp_cbs, 0, sizeof(ftp_cbs));
	ftp_cbs.userdata = req;
	ftp_cbs.complete = &main_dir_list_complete_cb;
	res =  arsdk_ftp_itf_create_req_list(itf->ftp_itf, &ftp_cbs,
			req->dev_type,
			ARSDK_FTP_SRV_TYPE_MEDIA,
			ARSDK_CRASHML_DIR_PATH,
			&req->ftp_list_req);
	if (res < 0)
		goto error;

	req->is_running = 1;

	list_add_after(&itf->reqs, &req->node);
	*ret_req = req;
	return 0;
error:
	arsdk_crashml_req_delete(req);
	return res;
}

int arsdk_crashml_req_cancel(struct arsdk_crashml_req *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	if (!req->is_running)
		return -EBUSY;

	req->is_running = 0;

	/* cancel main list request */
	if (req->ftp_list_req != NULL)
		arsdk_ftp_req_list_cancel(req->ftp_list_req);

	if (req->curr_req != NULL) {
		simple_req_cancel(req->curr_req);
		req->curr_req = NULL;
	}

	/* delete request */
	list_del(&req->node);
	arsdk_crashml_req_delete(req);

	return 0;
}

enum arsdk_device_type arsdk_crashml_req_get_dev_type(
		const struct arsdk_crashml_req *req)
{
	return req == NULL ? ARSDK_DEVICE_TYPE_UNKNOWN : req->dev_type;
}
