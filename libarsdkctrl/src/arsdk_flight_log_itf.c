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
#include <fnmatch.h>

#include "arsdk_flight_log_itf_priv.h"

#define ARSDK_FLIGHT_LOG_DIR_PATH "internal_000/fdr-lite/"
#define ARSDK_FLIGHT_LOG_TMP_EXT "tmp"
#define ARSDK_FLIGHT_LOG_BIN_EXT ".bin"

/** */
struct arsdk_flight_log_itf {
	struct arsdk_device_info                *dev_info;
	struct arsdk_ftp_itf                    *ftp_itf;
	struct list_node                        reqs;
};

struct arsdk_flight_log_req {
	struct arsdk_flight_log_itf             *itf;
	struct arsdk_flight_log_req_cbs         cbs;
	enum arsdk_device_type                  dev_type;
	char                                    *local_path;
	struct arsdk_ftp_req_list               *ftp_list_req;
	struct arsdk_ftp_file_list              *log_list;
	size_t                                  total;
	size_t                                  count;
	struct arsdk_ftp_file                   *curr_log;
	struct simple_req                       *curr_req;
	int                                     is_running;
	int                                     is_aborted;
	struct list_node                        node;
};

/** */
struct simple_req {
	struct arsdk_flight_log_req             *req;
	char                                    *name;
	char                                    *remote_path;
	char                                    *local_path;
	char                                    *local_path_tmp;
	struct arsdk_ftp_req_get                *ftp_get;
	struct arsdk_ftp_req_delete             *ftp_del;
	enum arsdk_flight_log_req_status        status;
	int                                     error;
};

/* forward declaration */
static int req_start_next(struct arsdk_flight_log_req *req);

int arsdk_flight_log_itf_new(struct arsdk_device_info *dev_info,
		struct arsdk_ftp_itf *ftp_itf,
		struct arsdk_flight_log_itf **ret_itf)
{
	struct arsdk_flight_log_itf *itf = NULL;

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

int arsdk_flight_log_itf_destroy(struct arsdk_flight_log_itf *itf)
{
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	arsdk_flight_log_itf_stop(itf);

	free(itf);
	return 0;
}

static int arsdk_flight_log_req_delete(struct arsdk_flight_log_req *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	if (req->log_list != NULL)
		arsdk_ftp_file_list_unref(req->log_list);

	free(req->local_path);
	free(req);

	return 0;
}

static int arsdk_flight_log_req_abort(struct arsdk_flight_log_req *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	req->is_aborted = 1;
	return arsdk_flight_log_req_cancel(req);
}

static int arsdk_flight_log_itf_abort_all(struct arsdk_flight_log_itf *itf)
{
	struct arsdk_flight_log_req *req = NULL;
	struct arsdk_flight_log_req *req_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	list_walk_entry_forward_safe(&itf->reqs, req, req_tmp, node) {
		arsdk_flight_log_req_abort(req);
	}

	return 0;
}

int arsdk_flight_log_itf_stop(struct arsdk_flight_log_itf *itf)
{
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	arsdk_flight_log_itf_abort_all(itf);

	return 0;
}

static enum arsdk_flight_log_req_status ftp_to_flight_log_status(
		enum arsdk_ftp_req_status ftp_status)
{
	switch (ftp_status) {
	case ARSDK_FTP_REQ_STATUS_OK:
		return ARSDK_FLIGHT_LOG_REQ_STATUS_OK;
	case ARSDK_FTP_REQ_STATUS_FAILED:
		return ARSDK_FLIGHT_LOG_REQ_STATUS_FAILED;
	case ARSDK_FTP_REQ_STATUS_CANCELED:
		return ARSDK_FLIGHT_LOG_REQ_STATUS_CANCELED;
	case ARSDK_FTP_REQ_STATUS_ABORTED:
		return ARSDK_FLIGHT_LOG_REQ_STATUS_ABORTED;
	default:
		ARSDK_LOGE("ftp status not known. %d", ftp_status);
		return ARSDK_FLIGHT_LOG_REQ_STATUS_FAILED;
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
 * end of a flight log itf run.
 */
static void req_done(struct arsdk_flight_log_req *req)
{
	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);

	if (req->is_running) {
		(*req->cbs.complete)(req->itf, req,
				ARSDK_FLIGHT_LOG_REQ_STATUS_OK, 0,
				req->cbs.userdata);
		list_del(&req->node);
		arsdk_flight_log_req_delete(req);
	} else {
		(*req->cbs.complete)(req->itf, req,
				ARSDK_FLIGHT_LOG_REQ_STATUS_ABORTED, 0,
				req->cbs.userdata);
	}
}

/**
 * end of a flight log current request.
 */
static void curr_req_done(struct arsdk_flight_log_req *req,
		enum arsdk_flight_log_req_status status, int error)
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

	if (req->curr_req->status != ARSDK_FLIGHT_LOG_REQ_STATUS_CANCELED &&
	    req->curr_req->status != ARSDK_FLIGHT_LOG_REQ_STATUS_ABORTED) {
		/* delete current request */
		simple_req_delete(req->curr_req);
		req->curr_req = NULL;
	}

	if (!req->is_running) {
		/*flight log is stopped */
		req_done(req);
		return;
	}

	/* start to download the next flight log */
	res = req_start_next(req);
	if (res < 0) {
		/* failed to start the next request */
		req_done(req);
	}
}

static void ftp_del_complete(struct arsdk_ftp_itf *ftp_itf,
		struct arsdk_ftp_req_delete *ftp_req,
		enum arsdk_ftp_req_status ftp_status,
		int error,
		void *userdata)
{
	struct arsdk_flight_log_req *req = userdata;
	struct simple_req *curr_req = NULL;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(req->curr_req != NULL, -EINVAL);
	curr_req = req->curr_req;

	if (curr_req->status == ARSDK_FLIGHT_LOG_REQ_STATUS_OK &&
	    ftp_status != ARSDK_FTP_REQ_STATUS_OK) {
		curr_req->status = ftp_to_flight_log_status(ftp_status);
		curr_req->error = error;
	}

	if (curr_req->status != ARSDK_FLIGHT_LOG_REQ_STATUS_OK) {
		curr_req_done(req, curr_req->status, curr_req->error);
		return;
	}

	/* current request done */
	curr_req_done(req, curr_req->status, curr_req->error);
}

static int ftp_del_log(struct arsdk_flight_log_req *req)
{
	int res = 0;
	struct simple_req *curr_req = NULL;
	struct arsdk_ftp_req_delete_cbs ftp_cbs = {
		.userdata = req,
		.complete = &ftp_del_complete,
	};

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->curr_req != NULL, -EINVAL);
	curr_req = req->curr_req;

	res = arsdk_ftp_itf_create_req_delete(req->itf->ftp_itf,
			&ftp_cbs, req->dev_type,
			ARSDK_FTP_SRV_TYPE_MEDIA, curr_req->remote_path,
			&curr_req->ftp_del);
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
	struct arsdk_flight_log_req *req = userdata;
	int res = 0;
	struct simple_req *curr_req = NULL;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(req->curr_req != NULL, -EINVAL);
	curr_req = req->curr_req;

	if (curr_req->status == ARSDK_FLIGHT_LOG_REQ_STATUS_OK &&
	    ftp_status != ARSDK_FTP_REQ_STATUS_OK) {
		curr_req->status = ftp_to_flight_log_status(ftp_status);
		curr_req->error = error;
		res = error;
	}

	if (curr_req->status != ARSDK_FLIGHT_LOG_REQ_STATUS_OK) {
		curr_req_done(req, curr_req->status, curr_req->error);
		return;
	}

	/* rename local log tmp */
	res = rename(curr_req->local_path_tmp, curr_req->local_path);
	if (res < 0) {
		ARSDK_LOG_ERRNO("rename failed", errno);
		curr_req_done(req, ARSDK_FLIGHT_LOG_REQ_STATUS_FAILED, -errno);
		return;
	}

	/* delete remote log file */
	res = ftp_del_log(req);
	if (res < 0)
		curr_req_done(req, ARSDK_FLIGHT_LOG_REQ_STATUS_FAILED, res);
}

static void ftp_get_progress_cb(struct arsdk_ftp_itf *itf,
		struct arsdk_ftp_req_get *req,
		float percent,
		void *userdata)
{
	/* do nothing */
}

static int ftp_get_log(struct arsdk_flight_log_req *req,
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

	res = arsdk_ftp_itf_create_req_get(req->itf->ftp_itf, &ftp_cbs,
			req->dev_type,
			ARSDK_FTP_SRV_TYPE_MEDIA,
			curr_req->remote_path,
			curr_req->local_path_tmp,
			0,
			&curr_req->ftp_get);
	if (res < 0)
		return res;

	return 0;
}

static int simple_req_new(struct arsdk_flight_log_req *req,
		const char *log_name,
		struct simple_req **ret_req)
{
	int64_t now = 0;
	int res = 0;
	struct simple_req *simple_req = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->itf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->itf->dev_info != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->local_path != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(log_name != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_req != NULL, -EINVAL);

	/* Allocate structure */
	simple_req = calloc(1, sizeof(*simple_req));
	if (simple_req == NULL)
		return -ENOMEM;

	simple_req->req = req;
	simple_req->name = xstrdup(log_name);
	if (simple_req->name == NULL) {
		res = -ENOMEM;
		goto error;
	}

	res = asprintf(&simple_req->remote_path, "%s%s",
			ARSDK_FLIGHT_LOG_DIR_PATH, log_name);
	if (res < 0) {
		res = -ENOMEM;
		goto error;
	}

	now = (int64_t)time(NULL);
	res = asprintf(&simple_req->local_path, "%s/%s_%04x_%"PRIi64"_%s",
			req->local_path,
			req->itf->dev_info->id,
			req->dev_type,
			now,
			log_name);
	if (res < 0) {
		res = -ENOMEM;
		goto error;
	}

	res = asprintf(&simple_req->local_path_tmp, "%s_%s",
			simple_req->local_path,
			ARSDK_FLIGHT_LOG_TMP_EXT);
	if (res < 0) {
		res = -ENOMEM;
		goto error;
	}

	/* get the log.bin */
	res = ftp_get_log(req, simple_req);
	if (res < 0)
		goto error;

	*ret_req = simple_req;

	return 0;
error:
	simple_req_delete(simple_req);
	return res;
}

static int simple_req_cancel(struct simple_req *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->req != NULL, -EINVAL);

	req->status =
		req->req->is_aborted ? ARSDK_FLIGHT_LOG_REQ_STATUS_ABORTED :
				       ARSDK_FLIGHT_LOG_REQ_STATUS_CANCELED;

	if (req->ftp_get != NULL)
		arsdk_ftp_req_get_cancel(req->ftp_get);

	if (req->ftp_del != NULL)
		arsdk_ftp_req_delete_cancel(req->ftp_del);

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

static int clean_local_dir(struct arsdk_flight_log_req *req)
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
			  strlen(ARSDK_FLIGHT_LOG_TMP_EXT))
			continue;


		/* check if the name start by the good device ID */
		res = strncmp(curr->d_name, req->itf->dev_info->id,
				strlen(req->itf->dev_info->id));
		if (res != 0)
			continue;

		/* check if the directory name end by "_tmp" */
		res = strncmp(&curr->d_name[len - 4],
				"_"ARSDK_FLIGHT_LOG_TMP_EXT,
				strlen("_"ARSDK_FLIGHT_LOG_TMP_EXT));
		if (res != 0)
			continue;

		/* delete old local directory tmp */
		res = snprintf(path, sizeof(path), "%s/%s",
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

static int is_flight_log(const char *name)
{
	int res = 0;

	if (name == NULL)
		return 0;

	res = fnmatch("log-*.bin", name, FNM_PATHNAME);
	return (res == 0);
}

static int req_start_next(struct arsdk_flight_log_req *req)
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
		req->curr_log = arsdk_ftp_file_list_next_file(
				req->log_list,
				req->curr_log);
		if (req->curr_log == NULL) {
			/* no next_flight_log */
			goto done;
		}

		name = arsdk_ftp_file_get_name(req->curr_log);
		if (!is_flight_log(name))
			continue;

		req->count++;
		res = simple_req_new(req, name, &req->curr_req);
		if (res < 0) {
			curr_req_done(req, ARSDK_FLIGHT_LOG_REQ_STATUS_FAILED,
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

static void main_log_list_complete_cb(struct arsdk_ftp_itf *ftp_itf,
		struct arsdk_ftp_req_list *ftp_req,
		enum arsdk_ftp_req_status ftp_status,
		int error,
		void *userdata)
{
	struct arsdk_flight_log_req *req = userdata;
	int res = 0;
	struct arsdk_ftp_file *curr = NULL;
	struct arsdk_ftp_file *next = NULL;
	const char *name = NULL;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);

	if (!req->is_running)
		goto done;

	if (ftp_status != ARSDK_FTP_REQ_STATUS_OK)
		goto done;

	req->log_list = arsdk_ftp_req_list_get_result(ftp_req);
	if (req->log_list == NULL)
		goto done;

	arsdk_ftp_file_list_ref(req->log_list);
	/* count total of flight log to download */
	next = arsdk_ftp_file_list_next_file(req->log_list, curr);
	while (next != NULL) {
		curr = next;
		next = arsdk_ftp_file_list_next_file(req->log_list, curr);

		name = arsdk_ftp_file_get_name(curr);
		if (is_flight_log(name))
			req->total++;
	}

	req->ftp_list_req = NULL;

	/* start to download the first flight log */
	/* or close and free the req if nothing is to do */
	res = req_start_next(req);
	if (res < 0)
		goto done;

	return;
done:
	req->ftp_list_req = NULL;
	req_done(req);
}

int arsdk_flight_log_itf_cancel_all(struct arsdk_flight_log_itf *itf)
{
	struct arsdk_flight_log_req *req = NULL;
	struct arsdk_flight_log_req *req_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	list_walk_entry_forward_safe(&itf->reqs, req, req_tmp, node) {
		arsdk_flight_log_req_cancel(req);
	}

	return 0;
}

static int similar_req_exists(struct arsdk_flight_log_itf *itf,
		enum arsdk_device_type dev_type)
{
	struct arsdk_flight_log_req *req = NULL;
	struct arsdk_flight_log_req *req_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	list_walk_entry_forward_safe(&itf->reqs, req, req_tmp, node) {
		if (req->dev_type == dev_type)
			return 1;
	}

	return 0;
}

int arsdk_flight_log_itf_create_req(struct arsdk_flight_log_itf *itf,
		const char *local_path,
		enum arsdk_device_type dev_type,
		const struct arsdk_flight_log_req_cbs *cbs,
		struct arsdk_flight_log_req **ret_req)
{
	struct arsdk_flight_log_req *req = NULL;
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

	req->local_path = xstrdup(local_path);
	if (req->local_path == NULL) {
		res = -ENOMEM;
		goto error;
	}

	memset(&ftp_cbs, 0, sizeof(ftp_cbs));
	ftp_cbs.userdata = req;
	ftp_cbs.complete = &main_log_list_complete_cb;

	res =  arsdk_ftp_itf_create_req_list(itf->ftp_itf, &ftp_cbs,
			req->dev_type,
			ARSDK_FTP_SRV_TYPE_MEDIA,
			ARSDK_FLIGHT_LOG_DIR_PATH,
			&req->ftp_list_req);
	if (res < 0)
		goto error;

	req->is_running = 1;

	list_add_after(&itf->reqs, &req->node);
	*ret_req = req;
	return 0;
error:
	arsdk_flight_log_req_delete(req);
	return res;
}

int arsdk_flight_log_req_cancel(struct arsdk_flight_log_req *req)
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
	arsdk_flight_log_req_delete(req);

	return 0;
}

enum arsdk_device_type arsdk_flight_log_req_get_dev_type(
		const struct arsdk_flight_log_req *req)
{
	return req == NULL ? ARSDK_DEVICE_TYPE_UNKNOWN : req->dev_type;
}
