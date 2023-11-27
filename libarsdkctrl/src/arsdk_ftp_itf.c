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
#include "arsdk_ftp_itf_priv.h"
#include "ftp/arsdk_ftp.h"
#include "arsdkctrl_default_log.h"

#include <sys/stat.h>

#ifdef BUILD_LIBMUX
#  include <libmux.h>
#endif /* BUILD_LIBMUX */

/** */
struct arsdk_ftp_itf {
	struct arsdk_transport             *transport;
	struct arsdk_ftp_itf_internal_cbs  internal_cbs;
	const struct arsdk_device_info     *dev_info;
	struct mux_ctx                     *mux;
	struct arsdk_ftp                   *ftp_ctx;
};

/** */
struct arsdk_ftp_file {
	uint32_t                        refcount;
	enum arsdk_ftp_file_type        type;
	const char                      *name;
	size_t                          size;
	struct list_node                node;
};

/** */
struct arsdk_ftp_req_base {
	struct arsdk_ftp_itf               *itf;
	void                               *child;
	const struct arsdk_ftp_req_ops     *ops;
	struct arsdk_ftp_req_cbs           ftpcbs;
	struct arsdk_ftp_req               *ftpreq;
	enum arsdk_device_type             dev_type;
	char                               *server;
	uint16_t                           port;
	struct mux_ip_proxy                *mux_ftp_proxy;
};

/** */
struct arsdk_ftp_req_put {
	struct arsdk_ftp_req_base       *base;
	struct arsdk_ftp_req_put_cbs    cbs;
	uint8_t                         is_resume;
	FILE                            *fin;
	char                            *remote_path;
	char                            *local_path;
	size_t                          buff_off;
	struct pomp_buffer              *buff;
	float                           ulpercent;
	size_t                          ulsize;
	size_t                          total_size;
	struct arsdk_ftp_req            *ftp_size_req;
};

/** */
struct arsdk_ftp_req_get {
	struct arsdk_ftp_req_base       *base;
	struct arsdk_ftp_req_get_cbs    cbs;
	FILE                            *fout;
	struct pomp_buffer              *buff;
	char                            *remote_path;
	char                            *local_path;
	float                           dlpercent;
	size_t                          dlsize;
	size_t                          total_size;
};

/** */
struct arsdk_ftp_req_rename {
	struct arsdk_ftp_req_base       *base;
	struct arsdk_ftp_req_rename_cbs cbs;
	char                            *src;
	char                            *dst;
};

/** */
struct arsdk_ftp_req_delete {
	struct arsdk_ftp_req_base       *base;
	struct arsdk_ftp_req_delete_cbs cbs;
	char                            *path;
};

struct arsdk_ftp_file_list {
	uint32_t                        refcount;
	struct list_node                files;
};

/** */
struct arsdk_ftp_req_list {
	struct arsdk_ftp_req_base       *base;
	struct arsdk_ftp_req_list_cbs   cbs;
	struct pomp_buffer              *buffer;
	char                            *path;
	struct arsdk_ftp_file_list      *result;
};

struct arsdk_ftp_req_ops {
	int (*start)(struct arsdk_ftp_req_base *req);
	void (*complete)(struct arsdk_ftp_req_base *req,
			enum arsdk_ftp_req_status status,
			int error);
	void (*progress)(struct arsdk_ftp_req_base *req,
			double dltotal, double dlnow, float dlpercent,
			double ultotal, double ulnow, float ulpercent);
	size_t (*write)(struct arsdk_ftp_req_base *req, const void *ptr,
			size_t size, size_t nmemb);
	size_t (*read)(struct arsdk_ftp_req_base *req, void *ptr,
			size_t size, size_t nmemb);
	void (*destroy)(struct arsdk_ftp_req_base *req);
};

#define DEFAULT_BUFFER_SIZE 256

/**
 */
static size_t default_read_data(struct arsdk_ftp_req_base *req,
		void *ptr,
		size_t size,
		size_t nmemb)
{
	return nmemb;
}

/**
 */
static size_t default_write_data(struct arsdk_ftp_req_base *req,
		const void *ptr,
		size_t size,
		size_t nmemb)
{
	return nmemb;
}

/**
 */
static void default_progress(struct arsdk_ftp_req_base *req,
		double dltotal, double dlnow, float dlpercent,
		double ultotal, double ulnow, float ulpercent)
{
	/* Do nothing. */
}

/**
 */
static void req_destroy(struct arsdk_ftp_req_base *req)
{
	int res = 0;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);

	if (req->ftpreq != NULL)
		ARSDK_LOGW("request %p still pending", req);

	if (req->mux_ftp_proxy != 0) {
		res = mux_ip_proxy_destroy(req->mux_ftp_proxy);
		if (res < 0)
			ARSDK_LOG_ERRNO("mux_ip_proxy_destroy", -res);
	}

	free(req->server);
	free(req);
}

/**
 */
static char *get_url(struct arsdk_ftp_req_base *req, const char *path)
{
	size_t len = strlen(req->server) + strlen(path) + 16;
	char *url = calloc(1, len);
	if (url == NULL)
		return NULL;
	snprintf(url, len, "ftp://%s:%u%s%s", req->server, req->port,
			path[0] == '/' ? "" : "/", path);
	return url;
}

/**
 */
static size_t req_read_data_cb(struct arsdk_ftp *ftp_ctx,
		struct arsdk_ftp_req *ftpreq,
		void *ptr,
		size_t size,
		size_t nmemb,
		void *userdata)
{
	struct arsdk_ftp_req_base *req = userdata;

	return (*req->ops->read)(req, ptr, size, nmemb);
}

/**
 */
static size_t req_write_data_cb(struct arsdk_ftp *ftp_ctx,
		struct arsdk_ftp_req *ftpreq,
		const void *ptr,
		size_t size,
		size_t nmemb,
		void *userdata)
{
	struct arsdk_ftp_req_base *req = userdata;

	return (*req->ops->write)(req, ptr, size, nmemb);
}

/**
 */
static void req_progress_cb(struct arsdk_ftp *ftp_ctx,
		struct arsdk_ftp_req *ftpreq,
		double dltotal, double dlnow, float dlpercent,
		double ultotal, double ulnow, float ulpercent,
		void *userdata)
{
	struct arsdk_ftp_req_base *req = userdata;

	/* Check if request is running */
	if (req->ftpreq == NULL)
		return;

	(*req->ops->progress)(req, dltotal, dlnow, dlpercent,
			ultotal, ulnow, ulpercent);
}

/**
 */
static void req_complete_cb(struct arsdk_ftp *ftp_ctx,
		struct arsdk_ftp_req *ftpreq,
		enum arsdk_ftp_status ftpstatus,
		int error,
		void *userdata)
{
	struct arsdk_ftp_req_base *req = userdata;
	enum arsdk_ftp_req_status status = 0;

	/* Convert status */
	switch (ftpstatus) {
	case ARSDK_FTP_STATUS_OK:
		status = ARSDK_FTP_REQ_STATUS_OK;
		break;
	case ARSDK_FTP_STATUS_CANCELED:
		status = ARSDK_FTP_REQ_STATUS_CANCELED;
		break;
	case ARSDK_FTP_STATUS_FAILED:
		status = ARSDK_FTP_REQ_STATUS_FAILED;
		break;
	case ARSDK_FTP_STATUS_ABORTED:
		status = ARSDK_FTP_REQ_STATUS_ABORTED;
		break;
	default:
		ARSDK_LOGW("Unknown ftp status: %d", ftpstatus);
		return;
	}

	/* Notify and cleanup request */
	(*req->ops->complete)(req, status, error);
	req->ftpreq = NULL;
	(*req->ops->destroy)(req);
}

/**
 */
static void socket_cb(struct arsdk_ftp *ftp_ctx,
		int fd,
		enum arsdk_socket_kind kind,
		void *userdata)
{
	struct arsdk_ftp_itf *itf = userdata;

	ARSDK_RETURN_IF_FAILED(itf != NULL, -EINVAL);

	/* socket hook callback */
	(*itf->internal_cbs.socketcb)(itf, fd, kind,
			itf->internal_cbs.userdata);
}

static int resolution(struct arsdk_ftp_itf *itf,
		enum arsdk_device_type dev_type,
		enum arsdk_ftp_srv_type srv_type,
		int *port,
		const char **host)
{
	static const int ftp_srv_ports[] = {
		[ARSDK_FTP_SRV_TYPE_MEDIA] = 21,
		[ARSDK_FTP_SRV_TYPE_UPDATE] = 51,
		[ARSDK_FTP_SRV_TYPE_FLIGHT_PLAN] = 61,
	};

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	*port = ftp_srv_ports[srv_type];

	switch (itf->dev_info->backend_type) {
	case ARSDK_BACKEND_TYPE_NET:
		if (dev_type != itf->dev_info->type &&
		    itf->dev_info->type != ARSDK_DEVICE_TYPE_SKYCTRL)
			*port += 100;

		*host = itf->dev_info->addr;
		return 0;
	case ARSDK_BACKEND_TYPE_MUX:
		if (dev_type == ARSDK_DEVICE_TYPE_SKYCTRL_2 ||
		    dev_type == ARSDK_DEVICE_TYPE_SKYCTRL_2P ||
		    dev_type == ARSDK_DEVICE_TYPE_SKYCTRL_NG ||
		    dev_type == ARSDK_DEVICE_TYPE_SKYCTRL_3  ||
		    dev_type == ARSDK_DEVICE_TYPE_SKYCTRL_UA ||
		    dev_type == ARSDK_DEVICE_TYPE_SKYCTRL_4 ||
		    dev_type == ARSDK_DEVICE_TYPE_SKYCTRL_4_BLACK)
			*host = "skycontroller";
		else
			*host = "drone";
		return 0;
	case ARSDK_BACKEND_TYPE_UNKNOWN:
	default:
		return -EINVAL;
	}
}

/**
 */
static void mux_proxy_open_cb(struct mux_ip_proxy *self, uint16_t localport,
			void *userdata)
{
	int res;
	struct arsdk_ftp_req_base *req = userdata;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);

	req->port = localport;

	res = (*req->ops->start)(req);
	if (res < 0) {
		/* Notify and cleanup request */
		(*req->ops->complete)(req, ARSDK_FTP_REQ_STATUS_FAILED, res);
		req->ftpreq = NULL;
		(*req->ops->destroy)(req);
	}
}

/**
 */
static void mux_proxy_close_cb(struct mux_ip_proxy *self, void *userdata)
{
	struct arsdk_ftp_req_base *req = userdata;

	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);

	req->port = 0;
}

/**
 */
static void idle_req_start_cb(void *userdata)
{
	int res;
	struct arsdk_ftp_req_base *req = userdata;

	res = (*req->ops->start)(req);
	if (res < 0) {
		/* Notify and cleanup request */
		(*req->ops->complete)(req, ARSDK_FTP_REQ_STATUS_FAILED, res);
		req->ftpreq = NULL;
		(*req->ops->destroy)(req);
	}
}

/**
 */
static int req_new(struct arsdk_ftp_itf *itf,
		enum arsdk_device_type dev_type,
		enum arsdk_ftp_srv_type srv_type,
		const struct arsdk_ftp_req_ops *ops,
		void *child,
		struct arsdk_ftp_req_base **ret_req)
{
	int res = 0;
	struct arsdk_ftp_req_base *req = NULL;
	int port = -1;
	const char *host = NULL;
	struct mux_ip_proxy_info mux_info = {
		.protocol = {
			.transport = MUX_IP_PROXY_TRANSPORT_TCP,
			.application = MUX_IP_PROXY_APPLICATION_FTP,
		},
	};
	struct mux_ip_proxy_cbs mux_cbs = {
		.open = &mux_proxy_open_cb,
		.close = &mux_proxy_close_cb,
	};

	ARSDK_RETURN_ERR_IF_FAILED(ops != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ops->start != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ops->complete != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ops->progress != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ops->read != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ops->write != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ops->destroy != NULL, -EINVAL);

	if (itf->transport == NULL)
		return -EPIPE;

	*ret_req = NULL;

	/* Allocate structure */
	req = calloc(1, sizeof(*req));
	if (req == NULL)
		return -ENOMEM;

	/* Initialize structure */
	req->itf = itf;
	req->ftpcbs.userdata = req;
	req->ftpcbs.read_data = &req_read_data_cb;
	req->ftpcbs.write_data = &req_write_data_cb;
	req->ftpcbs.progress = &req_progress_cb;
	req->ftpcbs.complete = &req_complete_cb;
	req->ops = ops;
	req->child = child;
	req->dev_type = dev_type;

	res = resolution(req->itf, dev_type, srv_type, &port, &host);
	if (res < 0)
		goto error;

	if (req->itf->mux == NULL) {
		req->port = port;
		req->server = strdup(host);

		pomp_loop_idle_add(
				arsdk_transport_get_loop(req->itf->transport),
				&idle_req_start_cb, req);

	} else {
		mux_info.remote_host = host;
		mux_info.remote_port = port;

		mux_cbs.userdata = req;

		/* Allocate mux tcp proxy */
		res = mux_ip_proxy_new(itf->mux, &mux_info, &mux_cbs, -1,
				&req->mux_ftp_proxy);
		if (res < 0) {
			ARSDK_LOG_ERRNO("mux_ip_proxy_new", -res);
			goto error;
		}
		req->server = strdup("127.0.0.1");
	}

	*ret_req = req;
	return 0;
error:
	req_destroy(req);
	return res;
}

/**
 */
int arsdk_ftp_itf_new(struct arsdk_transport *transport,
		const struct arsdk_ftp_itf_internal_cbs *internal_cbs,
		const struct arsdk_device_info *dev_info,
		struct mux_ctx *mux,
		struct arsdk_ftp_itf **ret_itf)
{
	int res = 0;
	struct arsdk_ftp_itf *itf = NULL;
	struct arsdk_ftp_cbs cbs;

	ARSDK_RETURN_ERR_IF_FAILED(ret_itf != NULL, -EINVAL);
	*ret_itf = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(transport != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(dev_info != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(internal_cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(internal_cbs->dispose != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(internal_cbs->socketcb != NULL, -EINVAL);

	/* Allocate structure */
	itf = calloc(1, sizeof(*itf));
	if (itf == NULL)
		return -ENOMEM;

	/* Initialize structure */
	itf->transport = transport;
	itf->internal_cbs = *internal_cbs;
	itf->dev_info = dev_info,
	itf->mux = mux;
#ifdef BUILD_LIBMUX
	if (itf->mux != NULL)
		mux_ref(itf->mux);
#endif /* BUILD_LIBMUX */

	memset(&cbs, 0, sizeof(cbs));
	cbs.socketcb = socket_cb;
	cbs.userdata = itf;

	/* Create ftp context */
	res = arsdk_ftp_new(arsdk_transport_get_loop(transport),
			NULL, NULL, &cbs, &itf->ftp_ctx);
	if (res < 0)
		goto error;

	*ret_itf = itf;
	return 0;

	/* Cleanup in case of error */
error:
	arsdk_ftp_itf_destroy(itf);
	return res;
}

/**
 */
int arsdk_ftp_itf_destroy(struct arsdk_ftp_itf *itf)
{
	int res = 0;

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	/* Stop interface */
	arsdk_ftp_itf_stop(itf);

	/* Notify internal callbacks */
	(*itf->internal_cbs.dispose)(itf, itf->internal_cbs.userdata);

	if (itf->ftp_ctx != NULL) {
		res = arsdk_ftp_destroy(itf->ftp_ctx);
		if (res < 0)
			ARSDK_LOG_ERRNO("arsdk_ftp_destroy", -res);
	}

#ifdef BUILD_LIBMUX
	if (itf->mux != NULL)
		mux_unref(itf->mux);
#endif /* BUILD_LIBMUX */
	free(itf);

	return 0;
}

/**
 */
int arsdk_ftp_itf_stop(struct arsdk_ftp_itf *itf)
{
	int res = 0;

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);
	itf->transport = NULL;
	res = arsdk_ftp_stop(itf->ftp_ctx);
	if (res < 0)
		ARSDK_LOG_ERRNO("arsdk_ftp_stop", -res);
	return 0;
}

int arsdk_ftp_itf_cancel_all(struct arsdk_ftp_itf *itf)
{
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	return arsdk_ftp_cancel_all(itf->ftp_ctx);
}

/* Get request : */


/**
 */
static void arsdk_ftp_req_get_destroy(struct arsdk_ftp_req_get *req_get)
{
	ARSDK_RETURN_IF_FAILED(req_get != NULL, -EINVAL);

	req_destroy(req_get->base);

	if (req_get->fout != NULL)
		fclose(req_get->fout);

	if (req_get->buff != NULL)
		pomp_buffer_unref(req_get->buff);

	free(req_get->local_path);
	free(req_get->remote_path);
	free(req_get);
}

static int req_get_start(struct arsdk_ftp_req_base *req)
{
	int res;
	struct arsdk_ftp_req_get *req_get = req->child;
	char *url = get_url(req_get->base, req_get->remote_path);
	if (url == NULL) {
		res = -ENOMEM;
		goto error;
	}

	res = arsdk_ftp_get(req_get->base->itf->ftp_ctx, &req_get->base->ftpcbs,
			url, req_get->dlsize, &req_get->base->ftpreq);
	if (res < 0)
		goto error;

	free(url);
	return 0;
error:
	free(url);
	return res;
}

static void req_get_destroy(struct arsdk_ftp_req_base *req)
{
	arsdk_ftp_req_get_destroy(req->child);
}

static size_t req_get_write_data(struct arsdk_ftp_req_base *req,
		const void *ptr,
		size_t size, size_t nmemb)
{
	int res = 0;
	size_t len = size * nmemb;
	struct arsdk_ftp_req_get *req_get = req->child;

	if (req_get->fout != NULL) {
		/* Write in output file */
		return fwrite(ptr, size, nmemb, req_get->fout);
	} else if (req_get->buff != NULL) {
		/* Write in pomp buffer */
		res = pomp_buffer_append_data(req_get->buff, ptr, len);
		if (res < 0)
			ARSDK_LOGE("pomp_buffer_append failed");

		return nmemb;
	}

	/* No write */
	ARSDK_LOGW("No output for req %p: size=%lu nmemb=%lu",
			req, (unsigned long)size, (unsigned long)nmemb);
	return nmemb;
}

/**
 */
static void req_get_progress(struct arsdk_ftp_req_base *req,
		double dltotal, double dlnow, float dlpercent,
		double ultotal, double ulnow, float ulpercent)
{
	struct arsdk_ftp_req_get *req_get = req->child;

	if ((req_get->fout == NULL) &&
	    (req_get->buff == NULL) &&
	    (dltotal > 0)) {
		/* Create the pomp buffer to Save data */
		req_get->buff = pomp_buffer_new(dltotal);
		if (req_get->buff == NULL) {
			ARSDK_LOGW("Failed to create buffer of capacity of %zu",
					(size_t)dltotal);
		}
	}

	if (req_get->dlpercent != dlpercent) {
		req_get->dlsize = dlnow;
		req_get->total_size = dltotal;
		req_get->dlpercent = dlpercent;
		(*req_get->cbs.progress)(req->itf, req_get,
				dlpercent, req_get->cbs.userdata);
	}
}

static void req_get_complete(struct arsdk_ftp_req_base *req,
		enum arsdk_ftp_req_status status, int error)
{
	struct arsdk_ftp_req_get *req_get = req->child;

	/* Notify */
	(*req_get->cbs.complete)(req->itf, req_get, status, error,
			req_get->cbs.userdata);
}

/**
 */
static const struct arsdk_ftp_req_ops s_req_get_ops = {
	.start = &req_get_start,
	.read = &default_read_data,
	.write = &req_get_write_data,
	.progress = &req_get_progress,
	.complete = &req_get_complete,
	.destroy = &req_get_destroy,
};

static int create_req_lpath(const char *local_path,
		const char *remote_path, char **ret_req_lpath)
{
	int res = 0;
	char *new_lpath = NULL;
	char *file_name = NULL;

	if (local_path[strlen(local_path) - 1] == '/') {
		/* directory local path */
		/* used the remote file name as local file name */
		/* get file name */
		file_name = strrchr(remote_path, '/');
		if (file_name == NULL)
			return -EINVAL;
		file_name += 1;

		/* format local file path */
		res = asprintf(&new_lpath, "%s%s", local_path, file_name);
		if (res < 0)
			return -ENOMEM;

	} else {
		new_lpath = xstrdup(local_path);
		if (new_lpath == NULL)
			return -ENOMEM;
	}

	*ret_req_lpath = new_lpath;
	return res;
}

/**
 */
int arsdk_ftp_itf_create_req_get(struct arsdk_ftp_itf *itf,
		const struct arsdk_ftp_req_get_cbs *cbs,
		enum arsdk_device_type dev_type,
		enum arsdk_ftp_srv_type srv_type,
		const char *remote_path,
		const char *local_path,
		uint8_t is_resume,
		struct arsdk_ftp_req_get **ret_req)
{
	int res = 0;
	struct arsdk_ftp_req_get *req_get = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_req != NULL, -EINVAL);
	*ret_req = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(remote_path != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->complete != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->progress != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(!(is_resume && local_path == NULL), -EINVAL);

	/* Allocate structure */
	req_get = calloc(1, sizeof(*req_get));
	if (req_get == NULL)
		return -ENOMEM;

	res = req_new(itf, dev_type, srv_type, &s_req_get_ops, req_get,
			&req_get->base);
	if (res < 0)
		goto error;

	if (local_path != NULL) {
		/* Save data in output file */

		res = create_req_lpath(local_path, remote_path,
				&req_get->local_path);
		if (res < 0)
			goto error;

		if (is_resume) {
			/* Append to the file */
			req_get->fout = fopen(req_get->local_path, "ab");
			if (req_get->fout == NULL) {
				res = -errno;
				ARSDK_LOGE("Failed to create '%s': err=%d(%s)",
						req_get->local_path,
						errno, strerror(errno));
				goto error;
			}

			/* update downloaded size */
			res = ftell(req_get->fout);
			if (res == -1) {
				res = -errno;
				ARSDK_LOG_ERRNO("ftell failed", errno);
				goto error;
			}
			req_get->dlsize = res;
		} else {
			/* overwrite the file */
			req_get->fout = fopen(req_get->local_path, "wb");
			if (req_get->fout == NULL) {
				res = -errno;
				ARSDK_LOGE("Failed to create '%s': err=%d(%s)",
						req_get->local_path,
						errno, strerror(errno));
				goto error;
			}
		}
	} /* Else Save data in pomp buffer */

	req_get->dlpercent = -1;
	req_get->remote_path = xstrdup(remote_path);
	req_get->cbs = *cbs;

	*ret_req = req_get;
	return 0;

error:
	arsdk_ftp_req_get_destroy(req_get);
	return res;
}

int arsdk_ftp_req_get_cancel(struct arsdk_ftp_req_get *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->base != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->base->itf != NULL, -EINVAL);

	return arsdk_ftp_cancel_req(req->base->itf->ftp_ctx,
			req->base->ftpreq);
}

const char *arsdk_ftp_req_get_get_remote_path(
		const struct arsdk_ftp_req_get *req)
{
	return req ? req->remote_path : NULL;
}

const char *arsdk_ftp_req_get_get_local_path(
		const struct arsdk_ftp_req_get *req)
{
	return req ? req->local_path : NULL;
}

struct pomp_buffer *arsdk_ftp_req_get_get_buffer(
		const struct arsdk_ftp_req_get *req)
{
	return req ? req->buff : NULL;
}

enum arsdk_device_type arsdk_ftp_req_get_get_dev_type(
		const struct arsdk_ftp_req_get *req)
{
	if ((req == NULL) ||
	    (req->base == NULL))
		return ARSDK_DEVICE_TYPE_UNKNOWN;

	return req->base->dev_type;
}

size_t arsdk_ftp_req_get_get_total_size(const struct arsdk_ftp_req_get *req)
{
	return req ? req->total_size : 0;
}

size_t arsdk_ftp_req_get_get_dlsize(const struct arsdk_ftp_req_get *req)
{
	return req ? req->dlsize : 0;
}

/* Put request : */

/**
 */
static void arsdk_ftp_req_put_destroy(struct arsdk_ftp_req_put *req_put)
{
	ARSDK_RETURN_IF_FAILED(req_put != NULL, -EINVAL);

	req_destroy(req_put->base);

	if (req_put->fin != NULL)
		fclose(req_put->fin);

	free(req_put->local_path);
	if (req_put->buff != NULL)
		pomp_buffer_unref(req_put->buff);
	free(req_put->remote_path);
	free(req_put);
}

static void req_put_destroy(struct arsdk_ftp_req_base *req)
{
	arsdk_ftp_req_put_destroy(req->child);
}

static size_t read_data_buff(struct arsdk_ftp_req_put *req_put,
		void *ptr, size_t size, size_t nmemb)
{
	int res = 0;
	const void *buff_data = NULL;
	size_t buff_len = 0;
	size_t size_to_read = 0;
	size_t new_off = 0;
	size_t size_read = 0;
	const void *buff_head = NULL;

	res = pomp_buffer_get_cdata(req_put->buff,
			&buff_data, &buff_len, NULL);
	if (res < 0) {
		ARSDK_LOG_ERRNO("pomp_buffer_get_cdata() failed", -res);
		return 0;
	}

	size_to_read = size * nmemb;
	new_off = req_put->buff_off + size_to_read;
	if (new_off > buff_len)
		new_off = buff_len;

	size_read = new_off - req_put->buff_off;
	buff_head = (const uint8_t *)buff_data + req_put->buff_off;
	memcpy(ptr, buff_head, size_read);

	req_put->buff_off = new_off;

	return size_read;
}

/**
- */
static size_t req_put_read_data(struct arsdk_ftp_req_base *req,
		void *ptr,
		size_t size,
		size_t nmemb)
{
	struct arsdk_ftp_req_put *req_put = req->child;

	if (req_put->fin != NULL) {
		return fread(ptr, size, nmemb, req_put->fin);
	} else if (req_put->buff != NULL) {
		return read_data_buff(req_put, ptr, size, nmemb);
	} else {
		ARSDK_LOGW("No intput for req %p: size=%lu nmemb=%lu",
				req, (unsigned long)size, (unsigned long)nmemb);
		return 0;
	}
}

/**
 */
static void req_put_progress(struct arsdk_ftp_req_base *req,
		double dltotal, double dlnow, float dlpercent,
		double ultotal, double ulnow, float ulpercent)
{
	struct arsdk_ftp_req_put *req_put = req->child;

	if (req_put->ulpercent != ulpercent) {
		req_put->ulpercent = ulpercent;
		req_put->ulsize = ulnow;

		/* Notify (callback is valid because we register low level one
		 * only in that case) */
		(*req_put->cbs.progress)(req->itf, req_put,
				ulpercent, req_put->cbs.userdata);
	}
}

static void req_put_complete(struct arsdk_ftp_req_base *req,
		enum arsdk_ftp_req_status status, int error)
{
	struct arsdk_ftp_req_put *req_put = req->child;

	/* Notify */
	(*req_put->cbs.complete)(req->itf, req_put, status, error,
			req_put->cbs.userdata);
}

static size_t size_read_data(struct arsdk_ftp *itf,
			struct arsdk_ftp_req *req,
			void *ptr,
			size_t size,
			size_t nmemb,
			void *userdata)
{
	/* Do nothing */

	return nmemb;
}

static size_t size_write_data(struct arsdk_ftp *ctx,
			struct arsdk_ftp_req *req,
			const void *ptr,
			size_t size,
			size_t nmemb,
			void *userdata)
{
	/* Do nothing */

	return nmemb;
}

static void size_progress(struct arsdk_ftp *ctx,
			struct arsdk_ftp_req *req,
			double dltotal,
			double dlnow,
			float dlpercent,
			double ultotal,
			double ulnow,
			float ulpercent,
			void *userdata)
{
	/* Do nothing */
}

static void size_complete(struct arsdk_ftp *ctx,
			struct arsdk_ftp_req *req,
			enum arsdk_ftp_status status,
			int error,
			void *userdata)
{
	int res = 0;
	size_t fsize = 0;
	const char *url = NULL;
	struct arsdk_ftp_req_put *req_put = userdata;

	if ((status == ARSDK_FTP_STATUS_CANCELED) ||
	    (status == ARSDK_FTP_STATUS_ABORTED)) {
		res = error;
		goto complete;
	}
	/* In case of ARSDK_FTP_SEQ_FAILED is not a resume but standard put */

	/* set the file cursor */
	fsize = arsdk_ftp_req_get_size(req);

	res = fseek(req_put->fin, fsize, SEEK_SET);
	if (res < 0) {
		res = -errno;
		ARSDK_LOG_ERRNO("fseek failed", errno);
		goto complete;
	}

	url = arsdk_ftp_req_get_url(req);
	res = arsdk_ftp_put(req_put->base->itf->ftp_ctx, &req_put->base->ftpcbs,
			url, fsize, req_put->total_size,
			&req_put->base->ftpreq);
	if (res < 0)
		goto complete;

	return;

complete:
	/* Notify */
	(*req_put->base->ftpcbs.complete)(ctx, req, status, error,
			req_put->base);
}

static int req_put_start(struct arsdk_ftp_req_base *req)
{
	int res = 0;
	struct arsdk_ftp_req_cbs req_size_cb;
	struct arsdk_ftp_req_put *req_put = req->child;
	char *url = get_url(req_put->base, req_put->remote_path);
	if (url == NULL) {
		res = -ENOMEM;
		goto error;
	}

	if (req_put->is_resume) {
		/* Send a size request before resume the request put */
		memset(&req_size_cb, 0, sizeof(req_size_cb));
		req_size_cb.read_data = &size_read_data;
		req_size_cb.write_data = &size_write_data;
		req_size_cb.progress = &size_progress;
		req_size_cb.complete = &size_complete;
		req_size_cb.userdata = req_put;
		res = arsdk_ftp_size(req->itf->ftp_ctx, &req_size_cb, url,
				&req_put->ftp_size_req);
		if (res < 0)
			goto error;
	} else {
		res = arsdk_ftp_put(req->itf->ftp_ctx, &req_put->base->ftpcbs,
				url, 0, req_put->total_size,
				&req_put->base->ftpreq);
		if (res < 0)
			goto error;
	}

	free(url);
	return 0;
error:
	free(url);
	return res;
}

/**
 */
static const struct arsdk_ftp_req_ops s_req_put_ops = {
	.start = &req_put_start,
	.read = &req_put_read_data,
	.write = &default_write_data,
	.progress = &req_put_progress,
	.complete = &req_put_complete,
	.destroy = &req_put_destroy,
};

/**
 */
static int create_req_put(struct arsdk_ftp_itf *itf,
		const struct arsdk_ftp_req_put_cbs *cbs,
		enum arsdk_device_type dev_type,
		enum arsdk_ftp_srv_type srv_type,
		const char *remote_path,
		const char *local_path,
		struct pomp_buffer *buffer,
		uint8_t is_resume,
		struct arsdk_ftp_req_put **ret_req)
{
	int res = 0;
	struct arsdk_ftp_req_put *req_put = NULL;
	struct stat stbuf;

	ARSDK_RETURN_ERR_IF_FAILED(ret_req != NULL, -EINVAL);
	*ret_req = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(remote_path != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED((local_path != NULL ||
				    buffer != NULL), -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->complete != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->progress != NULL, -EINVAL);

	/* Allocate structure */
	req_put = calloc(1, sizeof(*req_put));
	if (req_put == NULL)
		return -ENOMEM;

	res = req_new(itf, dev_type, srv_type, &s_req_put_ops, req_put,
			&req_put->base);
	if (res < 0)
		goto error;

	if (local_path != NULL) {
		req_put->fin = fopen(local_path, "rb");
		if (req_put->fin == NULL) {
			res = -errno;
			ARSDK_LOGE("Failed to create '%s': err=%d(%s)",
					local_path,
					errno, strerror(errno));
			goto error;
		}

		res = stat(local_path, &stbuf);
		if (res < 0) {
			res = -errno;
			ARSDK_LOG_ERRNO("stat() failed", errno);
			goto error;
		}

		if (!S_ISREG(stbuf.st_mode)) {
			res = -EINVAL;
			goto error;
		}

		req_put->total_size = stbuf.st_size;
		req_put->local_path = xstrdup(local_path);
	} else if (buffer != NULL) {
		res = pomp_buffer_get_cdata(buffer, NULL, &req_put->total_size,
				NULL);
		if (res < 0)
			goto error;

		pomp_buffer_ref(buffer);
		req_put->buff = buffer;
	}

	req_put->is_resume = is_resume;
	req_put->ulpercent = -1;
	req_put->remote_path = xstrdup(remote_path);
	req_put->cbs = *cbs;

	*ret_req = req_put;
	return 0;

error:
	arsdk_ftp_req_put_destroy(req_put);
	return res;
}

/**
 */
int arsdk_ftp_itf_create_req_put(struct arsdk_ftp_itf *itf,
		const struct arsdk_ftp_req_put_cbs *cbs,
		enum arsdk_device_type dev_type,
		enum arsdk_ftp_srv_type srv_type,
		const char *remote_path,
		const char *local_path,
		uint8_t is_resume,
		struct arsdk_ftp_req_put **ret_req)
{
	return create_req_put(itf, cbs, dev_type, srv_type, remote_path,
			local_path, NULL, is_resume, ret_req);
}

/**
 */
int arsdk_ftp_itf_create_req_put_buff(
		struct arsdk_ftp_itf *itf,
		const struct arsdk_ftp_req_put_cbs *cbs,
		enum arsdk_device_type dev_type,
		enum arsdk_ftp_srv_type srv_type,
		const char *remote_path,
		struct pomp_buffer *buffer,
		uint8_t is_resume,
		struct arsdk_ftp_req_put **ret_req)
{
	return create_req_put(itf, cbs, dev_type, srv_type, remote_path,
			NULL, buffer, is_resume, ret_req);
}

int arsdk_ftp_req_put_cancel(struct arsdk_ftp_req_put *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->base != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->base->itf != NULL, -EINVAL);

	return arsdk_ftp_cancel_req(req->base->itf->ftp_ctx, req->base->ftpreq);
}

const char *arsdk_ftp_req_put_get_remote_path(
		const struct arsdk_ftp_req_put *req)
{
	return req ? req->remote_path : NULL;
}

const char *arsdk_ftp_req_put_get_local_path(
		const struct arsdk_ftp_req_put *req)
{
	return req ? req->local_path : NULL;
}

enum arsdk_device_type arsdk_ftp_req_put_get_dev_type(
		const struct arsdk_ftp_req_put *req)
{
	if ((req == NULL) ||
	    (req->base == NULL))
		return ARSDK_DEVICE_TYPE_UNKNOWN;

	return req->base->dev_type;
}

size_t arsdk_ftp_req_put_get_total_size(const struct arsdk_ftp_req_put *req)
{
	return req ? req->total_size : 0;
}

size_t arsdk_ftp_req_put_get_ulsize(const struct arsdk_ftp_req_put *req)
{
	return req ? req->ulsize : 0;
}

/* Delete request : */

/**
 */
static void arsdk_ftp_req_delete_destroy(
	struct arsdk_ftp_req_delete *req_del)
{
	ARSDK_RETURN_IF_FAILED(req_del != NULL, -EINVAL);

	req_destroy(req_del->base);

	free(req_del->path);
	free(req_del);
}

static void req_delete_destroy(struct arsdk_ftp_req_base *req)
{
	arsdk_ftp_req_delete_destroy(req->child);
}

static void req_delete_complete(struct arsdk_ftp_req_base *req,
		enum arsdk_ftp_req_status status, int error)
{
	struct arsdk_ftp_req_delete *req_del = req->child;

	/* Notify */
	(*req_del->cbs.complete)(req->itf, req_del, status, error,
			req_del->cbs.userdata);
}

static int req_delete_start(struct arsdk_ftp_req_base *req)
{
	int res = 0;
	struct arsdk_ftp_req_delete *req_del = req->child;
	char *url = get_url(req_del->base, req_del->path);
	if (url == NULL) {
		res = -ENOMEM;
		goto error;
	}

	res = arsdk_ftp_delete(req_del->base->itf->ftp_ctx,
			&req_del->base->ftpcbs, url, &req_del->base->ftpreq);
	if (res < 0)
		goto error;

	free(url);
	return 0;
error:
	free(url);
	return res;
}

/**
 */
static const struct arsdk_ftp_req_ops s_req_delete_ops = {
	.start = &req_delete_start,
	.read = &default_read_data,
	.write = &default_write_data,
	.progress = &default_progress,
	.complete = &req_delete_complete,
	.destroy = &req_delete_destroy,
};

int arsdk_ftp_itf_create_req_delete(
		struct arsdk_ftp_itf *itf,
		const struct arsdk_ftp_req_delete_cbs *cbs,
		enum arsdk_device_type dev_type,
		enum arsdk_ftp_srv_type srv_type,
		const char *remote_path,
		struct arsdk_ftp_req_delete **ret_req)
{
	int res = 0;
	struct arsdk_ftp_req_delete *req_del = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_req != NULL, -EINVAL);
	*ret_req = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(remote_path != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->complete != NULL, -EINVAL);

	/* Allocate structure */
	req_del = calloc(1, sizeof(*req_del));
	if (req_del == NULL)
		return -ENOMEM;

	res = req_new(itf, dev_type, srv_type, &s_req_delete_ops, req_del,
			&req_del->base);
	if (res < 0)
		goto error;

	req_del->path = xstrdup(remote_path);
	req_del->cbs = *cbs;

	*ret_req = req_del;
	return 0;

error:
	arsdk_ftp_req_delete_destroy(req_del);
	return res;
}

int arsdk_ftp_req_delete_cancel(struct arsdk_ftp_req_delete *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->base != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->base->itf != NULL, -EINVAL);

	return arsdk_ftp_cancel_req(req->base->itf->ftp_ctx,
			req->base->ftpreq);
}

const char *arsdk_ftp_req_delete_get_path(
		const struct arsdk_ftp_req_delete *req)
{
	return req ? req->path : NULL;
}

enum arsdk_device_type arsdk_ftp_req_delete_get_dev_type(
		const struct arsdk_ftp_req_delete *req)
{
	if ((req == NULL) ||
	    (req->base == NULL))
		return ARSDK_DEVICE_TYPE_UNKNOWN;

	return req->base->dev_type;
}

/* Rename request : */

/**
 */
static void arsdk_ftp_req_rename_destroy(
	struct arsdk_ftp_req_rename *req_rename)
{
	ARSDK_RETURN_IF_FAILED(req_rename != NULL, -EINVAL);

	req_destroy(req_rename->base);

	free(req_rename->src);
	free(req_rename->dst);
	free(req_rename);
}

static void req_rename_destroy(struct arsdk_ftp_req_base *req)
{
	arsdk_ftp_req_rename_destroy(req->child);
}

static void req_rename_complete(struct arsdk_ftp_req_base *req,
		enum arsdk_ftp_req_status status, int error)
{
	struct arsdk_ftp_req_rename *req_rename = req->child;

	/* Notify */
	(*req_rename->cbs.complete)(req->itf, req_rename, status, error,
			req_rename->cbs.userdata);
}

static int req_rename_start(struct arsdk_ftp_req_base *req)
{
	int res = 0;
	struct arsdk_ftp_req_rename *req_rename = req->child;

	char *url = get_url(req_rename->base, req_rename->src);
	if (url == NULL) {
		res = -ENOMEM;
		goto error;
	}

	res = arsdk_ftp_rename(req->itf->ftp_ctx, &req_rename->base->ftpcbs,
			url, req_rename->dst, &req_rename->base->ftpreq);
	if (res < 0)
		goto error;

	free(url);
	return 0;
error:
	free(url);
	return res;
}

/**
 */
static const struct arsdk_ftp_req_ops s_req_rename_ops = {
	.start = &req_rename_start,
	.read = &default_read_data,
	.write = &default_write_data,
	.progress = &default_progress,
	.complete = &req_rename_complete,
	.destroy = &req_rename_destroy,
};

int arsdk_ftp_itf_create_req_rename(
		struct arsdk_ftp_itf *itf,
		const struct arsdk_ftp_req_rename_cbs *cbs,
		enum arsdk_device_type dev_type,
		enum arsdk_ftp_srv_type srv_type,
		const char *src,
		const char *dst,
		struct arsdk_ftp_req_rename **ret_req)
{
	int res = 0;
	struct arsdk_ftp_req_rename *req_rename = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_req != NULL, -EINVAL);
	*ret_req = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(src != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(dst != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->complete != NULL, -EINVAL);

	/* Allocate structure */
	req_rename = calloc(1, sizeof(*req_rename));
	if (req_rename == NULL)
		return -ENOMEM;

	res = req_new(itf, dev_type, srv_type, &s_req_rename_ops,
			req_rename, &req_rename->base);
	if (res < 0)
		goto error;

	req_rename->src = xstrdup(src);
	req_rename->dst = xstrdup(dst);
	req_rename->cbs = *cbs;

	*ret_req = req_rename;
	return 0;

error:
	arsdk_ftp_req_rename_destroy(req_rename);
	return res;
}

int arsdk_ftp_req_rename_cancel(struct arsdk_ftp_req_rename *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->base != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->base->itf != NULL, -EINVAL);

	return arsdk_ftp_cancel_req(req->base->itf->ftp_ctx,
			req->base->ftpreq);
}

const char *arsdk_ftp_req_rename_get_src(
		const struct arsdk_ftp_req_rename *req)
{
	return req ? req->src : NULL;
}

const char *arsdk_ftp_req_rename_get_dst(
		const struct arsdk_ftp_req_rename *req)
{
	return req ? req->dst : NULL;
}

enum arsdk_device_type arsdk_ftp_req_rename_get_dev_type(
		const struct arsdk_ftp_req_rename *req)
{
	if ((req == NULL) ||
	    (req->base == NULL))
		return ARSDK_DEVICE_TYPE_UNKNOWN;

	return req->base->dev_type;
}

static int arsdk_ftp_file_list_destroy(struct arsdk_ftp_file_list *list)
{
	struct arsdk_ftp_file *file;
	struct arsdk_ftp_file *file_tmp;

	ARSDK_RETURN_ERR_IF_FAILED(list != NULL, -EINVAL);

	list_walk_entry_forward_safe(&list->files, file, file_tmp, node) {
		list_del(&file->node);
		arsdk_ftp_file_unref(file);
	}

	free(list);

	return 0;
}

static int arsdk_ftp_file_list_new(struct arsdk_ftp_file_list **ret_list)
{
	struct arsdk_ftp_file_list *list = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_list != NULL, -EINVAL);

	list = calloc(1, sizeof(*list));
	if (list == NULL)
		return -ENOMEM;

	list->refcount = 1;
	list_init(&list->files);

	*ret_list = list;
	return 0;
}

/* List request : */

/**
 */
static void arsdk_ftp_req_list_destroy(
	struct arsdk_ftp_req_list *req_list)
{
	ARSDK_RETURN_IF_FAILED(req_list != NULL, -EINVAL);

	arsdk_ftp_file_list_unref(req_list->result);
	req_destroy(req_list->base);
	if (req_list->buffer != NULL)
		pomp_buffer_unref(req_list->buffer);
	free(req_list->path);
	free(req_list);
}

static void req_list_destroy(struct arsdk_ftp_req_base *req)
{
	arsdk_ftp_req_list_destroy(req->child);
}

static size_t req_list_write_data(struct arsdk_ftp_req_base *req,
		const void *ptr, size_t size, size_t nmemb)
{
	int res = 0;
	struct arsdk_ftp_req_list *req_list = req->child;
	size_t len = size * nmemb;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req_list != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req_list->buffer != NULL, -EINVAL);

	res = pomp_buffer_append_data(req_list->buffer, ptr, len);
	if (res < 0) {
		ARSDK_LOGE("pomp_buffer_append failed");
		return nmemb;
	}

	return nmemb;
}

static int list_line_to_file(const char *line, struct arsdk_ftp_file *file)
{
	int res = 0;
	char perm[11];
	char name[256];

	ARSDK_RETURN_ERR_IF_FAILED(line != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(file != NULL, -EINVAL);

	res = sscanf(line, "%10s %*d %*d %*d %zu %*s %*u %*[0-9:] %255s",
			perm,
			&file->size,
			name);
	if (res < 3) {
		ARSDK_LOGW("Failed to parse ftp list line. \"%s\"", line);
		return -EINVAL;
	}

	file->name = xstrdup(name);

	switch (perm[0]) {
	case 'd':
		file->type = ARSDK_FTP_FILE_TYPE_DIR;
		break;
	case 'l':
		file->type = ARSDK_FTP_FILE_TYPE_LINK;
		break;
	case '-':
	default:
		file->type = ARSDK_FTP_FILE_TYPE_FILE;
		break;
	}

	return 0;
}

/**
 */
int arsdk_ftp_file_new(struct arsdk_ftp_file **ret_file)
{
	struct arsdk_ftp_file *file = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_file != NULL, -EINVAL);

	file = calloc(1, sizeof(*file));
	if (file == NULL)
		return -ENOMEM;

	file->refcount = 1;

	*ret_file = file;
	return 0;
}

/**
 */
void arsdk_ftp_file_destroy(struct arsdk_ftp_file *file)
{
	ARSDK_RETURN_IF_FAILED(file != NULL, -EINVAL);

	free((char *)file->name);
	free(file);
}

/**
 */
int arsdk_ftp_file_set_name(struct arsdk_ftp_file *file,
		const char *name)
{
	ARSDK_RETURN_ERR_IF_FAILED(file != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(name != NULL, -EINVAL);

	file->name = xstrdup(name);
	if (file->name == NULL)
		return -ENOMEM;

	return 0;
}

/**
 */
static void req_list_complete(struct arsdk_ftp_req_base *req,
		enum arsdk_ftp_req_status status, int error)
{
	static const char *total_str = "total";
	char *data = NULL;
	size_t len = 0;
	size_t capacity = 0;
	int res = 0;
	char *tok = NULL;
	char *line = NULL;
	struct arsdk_ftp_file *file = NULL;
	struct arsdk_ftp_file_list *response = NULL;
	struct arsdk_ftp_req_list *req_list = req->child;

	if (status != ARSDK_FTP_REQ_STATUS_OK) {
		res = error;
		goto end;
	}

	/* Append null character to the buffer */
	res = pomp_buffer_append_data(req_list->buffer, "", 1);
	if (res < 0) {
		ARSDK_LOGE("pomp_buffer_get_cdata failed.");
		status = ARSDK_FTP_REQ_STATUS_FAILED;
		goto end;
	}

	res = pomp_buffer_get_data(req_list->buffer, (void **)&data,
			&len, &capacity);
	if (res < 0) {
		ARSDK_LOGE("pomp_buffer_get_cdata failed.");
		status = ARSDK_FTP_REQ_STATUS_FAILED;
		goto end;
	}

	if (data == NULL) {
		ARSDK_LOGE("No data to parse.");
		status = ARSDK_FTP_REQ_STATUS_FAILED;
		goto end;
	}

	res = arsdk_ftp_file_list_new(&response);
	if (res < 0) {
		status = ARSDK_FTP_REQ_STATUS_FAILED;
		res = -ENOMEM;
		goto end;
	}
	req_list->result = response;

	line = strtok_r(data, "\n", &tok);
	if (line == NULL)
		goto end;

	/* skip the first line if it starts by "total". */
	res = strncmp(line, total_str, sizeof(*total_str));
	if (res == 0)
		line = strtok_r(NULL, "\n", &tok);

	while (line != NULL) {
		res = arsdk_ftp_file_new(&file);
		if (res < 0) {
			status = ARSDK_FTP_REQ_STATUS_FAILED;
			goto end;
		}

		res = list_line_to_file(line, file);
		if (res == 0)
			list_add_after(&response->files, &file->node);
		else
			arsdk_ftp_file_unref(file);

		line = strtok_r(NULL, "\n", &tok);
	}

end:
	/* list callback */
	(*req_list->cbs.complete)(req->itf, req_list,
			status, res, req_list->cbs.userdata);
	return;
}

static int req_list_start(struct arsdk_ftp_req_base *req)
{
	int res = 0;
	struct arsdk_ftp_req_list *req_list = req->child;
	char *url = get_url(req_list->base, req_list->path);
	if (url == NULL) {
		res = -ENOMEM;
		goto error;
	}

	res = arsdk_ftp_list(req->itf->ftp_ctx, &req_list->base->ftpcbs,
			url, &req_list->base->ftpreq);
	if (res < 0)
		goto error;

	free(url);
	return 0;
error:
	free(url);
	return res;
}

/**
 */
static const struct arsdk_ftp_req_ops s_req_list_ops = {
	.start = &req_list_start,
	.read = &default_read_data,
	.write = &req_list_write_data,
	.progress = &default_progress,
	.complete = &req_list_complete,
	.destroy = &req_list_destroy,
};

int arsdk_ftp_itf_create_req_list(
	struct arsdk_ftp_itf *itf,
	const struct arsdk_ftp_req_list_cbs *cbs,
	enum arsdk_device_type dev_type,
	enum arsdk_ftp_srv_type srv_type,
	const char *remote_path,
	struct arsdk_ftp_req_list **ret_req)
{
	int res = 0;
	struct arsdk_ftp_req_list *req_list = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_req != NULL, -EINVAL);
	*ret_req = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(remote_path != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->complete != NULL, -EINVAL);

	/* Allocate structure */
	req_list = calloc(1, sizeof(*req_list));
	if (req_list == NULL)
		return -ENOMEM;

	res = req_new(itf, dev_type, srv_type, &s_req_list_ops, req_list,
			&req_list->base);
	if (res < 0)
		goto error;

	req_list->path = xstrdup(remote_path);
	req_list->cbs = *cbs;
	req_list->buffer = pomp_buffer_new(DEFAULT_BUFFER_SIZE);
	if (req_list->buffer == NULL) {
		res = -ENOMEM;
		goto error;
	}

	*ret_req = req_list;
	return 0;

error:
	arsdk_ftp_req_list_destroy(req_list);
	return res;
}

int arsdk_ftp_req_list_cancel(struct arsdk_ftp_req_list *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->base != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->base->itf != NULL, -EINVAL);

	return arsdk_ftp_cancel_req(req->base->itf->ftp_ctx,
			req->base->ftpreq);
}

const char *arsdk_ftp_req_list_get_path(
		const struct arsdk_ftp_req_list *req)
{
	return req ? req->path : NULL;
}

enum arsdk_device_type arsdk_ftp_req_list_get_dev_type(
		const struct arsdk_ftp_req_list *req)
{
	if ((req == NULL) ||
	    (req->base == NULL))
		return ARSDK_DEVICE_TYPE_UNKNOWN;

	return req->base->dev_type;
}

struct arsdk_ftp_file_list *arsdk_ftp_req_list_get_result(
		struct arsdk_ftp_req_list *req)
{
	if (req == NULL)
		return NULL;

	return req->result;
}

/*
 * File API:
 */

struct arsdk_ftp_file *arsdk_ftp_file_list_next_file(
		struct arsdk_ftp_file_list *list,
		struct arsdk_ftp_file *prev)
{
	struct list_node *node;
	struct arsdk_ftp_file *next;

	if (!list)
		return NULL;

	node = list_next(&list->files, prev ? &prev->node : &list->files);
	if (!node)
		return NULL;

	next = list_entry(node, struct arsdk_ftp_file, node);
	return next;
}

size_t arsdk_ftp_file_list_get_count(
		struct arsdk_ftp_file_list *list)
{
	if (!list)
		return 0;

	return list_length(&list->files);
}

void arsdk_ftp_file_list_ref(struct arsdk_ftp_file_list *list)
{
	ARSDK_RETURN_IF_FAILED(list != NULL, -EINVAL);

	list->refcount++;
}

void arsdk_ftp_file_list_unref(struct arsdk_ftp_file_list *list)
{
	ARSDK_RETURN_IF_FAILED(list != NULL, -EINVAL);

	list->refcount--;

	/* Free resource when ref count reaches 0 */
	if (list->refcount == 0)
		(void)arsdk_ftp_file_list_destroy(list);
}

const char *arsdk_ftp_file_get_name(const struct arsdk_ftp_file *file)
{
	return (file != NULL) ? file->name : NULL;
}

size_t arsdk_ftp_file_get_size(const struct arsdk_ftp_file *file)
{
	return (file != NULL) ? file->size : 0;
}

ARSDK_API enum arsdk_ftp_file_type arsdk_ftp_file_get_type(
		const struct arsdk_ftp_file *file)
{
	if (file == NULL)
		return ARSDK_FTP_FILE_TYPE_UNKNOWN;

	return file->type;
}

/*
 * See documentation in public header.
 */
void arsdk_ftp_file_ref(struct arsdk_ftp_file *file)
{
	ARSDK_RETURN_IF_FAILED(file != NULL, -EINVAL);

	file->refcount++;
}

/*
 * See documentation in public header.
 */
void arsdk_ftp_file_unref(struct arsdk_ftp_file *file)
{
	ARSDK_RETURN_IF_FAILED(file != NULL, -EINVAL);

	file->refcount--;

	/* Free resource when ref count reaches 0 */
	if (file->refcount == 0)
		(void)arsdk_ftp_file_destroy(file);
}
