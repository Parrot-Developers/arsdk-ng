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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <libpomp.h>
#include <libmux.h>

#include "arsdkctrl/arsdkctrl.h"
#define ULOG_TAG controller
#include "ulog.h"
ULOG_DECLARE_TAG(controller);

#ifdef BUILD_LIBPUF
#  include <libpuf.h>
#endif /* !BUILD_LIBPUF */

#define LOGD(_fmt, ...)	ULOGD(_fmt, ##__VA_ARGS__)
#define LOGI(_fmt, ...)	ULOGI(_fmt, ##__VA_ARGS__)
#define LOGW(_fmt, ...)	ULOGW(_fmt, ##__VA_ARGS__)
#define LOGE(_fmt, ...)	ULOGE(_fmt, ##__VA_ARGS__)

/** Log error with errno */
#define LOG_ERRNO(_fct, _err) \
	LOGE("%s:%d: %s err=%d(%s)", __func__, __LINE__, \
			_fct, _err, strerror(_err))

/** Log error with fd and errno */
#define LOG_FD_ERRNO(_fct, _fd, _err) \
	LOGE("%s:%d: %s(fd=%d) err=%d(%s)", __func__, __LINE__, \
			_fct, _fd, _err, strerror(_err))

#define PCMD_PERIOD_DRONE 50
#define PCMD_PERIOD_JS 50

/** */
struct app {
	int                             stopped;
	enum arsdk_backend_type         backend_type;
	struct pomp_loop                *loop;
	struct pomp_timer               *timer;
	struct arsdk_ctrl               *ctrl;
	struct arsdkctrl_backend_mux    *backend_mux;
	struct arsdk_discovery_mux      *discovery_mux;
	struct arsdkctrl_backend_net    *backend_net;
	struct arsdk_discovery_avahi    *discovery_avahi;
	struct arsdk_discovery_net      *discovery_net;
	char                            *net_device_ip;
	int                             use_discovery_net;
	int                             use_discovery_avahi;
	struct arsdk_device             *device;
	struct arsdk_cmd_itf            *cmd_itf;
	struct {
		struct mux_ctx    *muxctx;
		struct pomp_ctx   *pompctx;
		char              *bridge_ip;
		int               port;
	} mux;
	struct {
		int                     used;
		struct arsdk_ftp_itf    *itf;
		enum arsdk_device_type  dev_type;
		struct {
			char            *local_path;
			char            *remote_path;
		} get;
		struct {
			char            *local_path;
			char            *remote_path;
		} put;
		struct {
			char            *path;
		} list;
		struct {
			char            *path;
		} delete;
		struct {
			char            *src;
			char            *dst;
		} rename;
	} ftp;

	struct {
		int                     used;
		struct arsdk_media_itf  *itf;
		enum arsdk_device_type  dev_type;
		struct {
			int             used;
			uint32_t types;
		} list;
		struct {
			char            *uri;
			char            *local_path;
		} dl;
		struct {
			char            *name;
		} del;
	} media;

	struct {
		int                             used;
		struct arsdk_updater_itf        *itf;
		enum arsdk_device_type          dev_type;
		char                            *fw_filepath;
		char                            *fw2_filepath;
	} update;

	struct {
		int                             used;
		struct arsdk_crashml_itf        *itf;
		enum arsdk_device_type          dev_type;
		char                            *dir_path;
		uint32_t                        types;
	} crashml;

	struct {
		int                             used;
		struct arsdk_flight_log_itf     *itf;
		enum arsdk_device_type          dev_type;
		char                            *dir_path;
	} flight_log;

	struct {
		int                             used;
		struct arsdk_pud_itf            *itf;
		enum arsdk_device_type          dev_type;
		char                            *dir_path;
	} pud;

	struct {
		int                             used;
		struct arsdk_ephemeris_itf      *itf;
		enum arsdk_device_type          dev_type;
		char                            *file_path;
	} ephemeris;

	struct {
		int                             used;
		struct arsdk_blackbox_itf       *itf;
		struct arsdk_blackbox_listener  *listener;
	} blackbox;

	struct {
		int                             used;
		enum arsdk_device_type          dev_type;
		struct pomp_ctx                 *pompctx;
		char                            *data;
		int                             port;
		struct arsdk_device_tcp_proxy   *proxy;
	} tcp_send;
};

/** */
static struct app s_app = {
	.stopped = 0,
	.backend_type = ARSDK_BACKEND_TYPE_UNKNOWN,
	.loop = NULL,
	.timer = NULL,
	.ctrl = NULL,
	.backend_net = NULL,
	.backend_mux = NULL,
	.discovery_mux = NULL,
	.discovery_avahi = NULL,
	.discovery_net = NULL,
	.net_device_ip = NULL,
	.use_discovery_net = 0,
	.use_discovery_avahi = 1,
	.device = NULL,
	.cmd_itf = NULL,
	.mux = {
		.muxctx = NULL,
		.pompctx = NULL,
		.port = 4321,
		.bridge_ip = NULL,
	},
	.ftp = {
		.used = 0,
		.itf = NULL,
		.dev_type = 0,
		.get = {
			.local_path = NULL,
			.remote_path = NULL,
		},
		.put = {
			.local_path = NULL,
			.remote_path = NULL,
		},
		.list = {
			.path = NULL,
		},
		.delete = {
			.path = NULL,
		},
		.rename = {
			.src = NULL,
			.dst = NULL,
		},
	},
	.media = {
		.used = 0,
		.itf = NULL,
		.dev_type = 0,
		.list = {
			.used = 0,
			.types = ARSDK_MEDIA_TYPE_ALL,
		},
		.dl = {
			.uri = NULL,
			.local_path = NULL,
		},
		.del = {
			.name = NULL,
		},
	},
	.update = {
		.used = 0,
		.itf = NULL,
		.dev_type = 0,
		.fw_filepath = NULL,
		.fw2_filepath = NULL,
	},
	.crashml = {
		.used = 0,
		.itf = NULL,
		.dev_type = 0,
		.dir_path = NULL,
		.types = ARSDK_CRASHML_TYPE_DIR | ARSDK_CRASHML_TYPE_TARGZ,
	},
	.flight_log = {
		.used = 0,
		.itf = NULL,
		.dev_type = 0,
		.dir_path = NULL,
	},
	.pud = {
		.used = 0,
		.itf = NULL,
		.dev_type = 0,
		.dir_path = NULL,
	},
	.ephemeris = {
		.used = 0,
		.itf = NULL,
		.dev_type = 0,
		.file_path = NULL,
	},
	.blackbox = {
		.used = 0,
		.itf = NULL,
		.listener= NULL,
	},
	.tcp_send = {
		.used = 0,
		.pompctx = NULL,
		.dev_type = 0,
		.data = NULL,
		.port = 0,
		.proxy = NULL,
	},
};

static void ftp_get_progress_cb(struct arsdk_ftp_itf *itf,
		struct arsdk_ftp_req_get *req,
		float percent,
		void *userdata)
{
	const char *local_path = NULL;
	const char *remote_path = NULL;

	local_path = arsdk_ftp_req_get_get_local_path(req);
	remote_path = arsdk_ftp_req_get_get_remote_path(req);
	LOGI("ftp get of remote:%s in local: %s progress: %f%%",
			remote_path, local_path, percent);
}

static void ftp_get_complete_cb(struct arsdk_ftp_itf *itf,
		struct arsdk_ftp_req_get *req,
		enum arsdk_ftp_req_status status,
		int error,
		void *userdata)
{
	const char *local_path = NULL;
	const char *remote_path = NULL;

	local_path = arsdk_ftp_req_get_get_local_path(req);
	remote_path = arsdk_ftp_req_get_get_remote_path(req);
	LOGI("ftp get of remote:%s in local: %s status: %d",
			remote_path, local_path, status);
}

static struct arsdk_ftp_req_get_cbs s_ftp_get_cbs = {
	.userdata = &s_app,
	.progress = &ftp_get_progress_cb,
	.complete = &ftp_get_complete_cb,
};

static void ftp_put_complete_cb(struct arsdk_ftp_itf *itf,
		struct arsdk_ftp_req_put *req,
		enum arsdk_ftp_req_status status,
		int error,
		void *userdata)
{
	const char *local_path = NULL;
	const char *remote_path = NULL;

	local_path = arsdk_ftp_req_put_get_local_path(req);
	remote_path = arsdk_ftp_req_put_get_remote_path(req);
	LOGI("ftp put of remote:%s in local: %s status: %d",
			remote_path, local_path, status);
}

static void ftp_put_progress_cb(struct arsdk_ftp_itf *itf,
		struct arsdk_ftp_req_put *req,
		float percent,
		void *userdata)
{
	const char *local_path = NULL;
	const char *remote_path = NULL;

	local_path = arsdk_ftp_req_put_get_local_path(req);
	remote_path = arsdk_ftp_req_put_get_remote_path(req);
	LOGI("ftp put of remote:%s in local: %s progress: %f%%",
			remote_path, local_path, percent);
}

static struct arsdk_ftp_req_put_cbs s_ftp_put_cbs = {
	.userdata = &s_app,
	.progress = &ftp_put_progress_cb,
	.complete = &ftp_put_complete_cb,
};

static void ftp_rename_complete_cb(struct arsdk_ftp_itf *itf,
		struct arsdk_ftp_req_rename *req,
		enum arsdk_ftp_req_status status,
		int error,
		void *userdata)
{
	const char *src = NULL;
	const char *dst = NULL;

	src = arsdk_ftp_req_rename_get_src(req);
	dst = arsdk_ftp_req_rename_get_dst(req);

	LOGI("ftp rename of %s in %s status : %d", src, dst, status);
}

static struct arsdk_ftp_req_rename_cbs s_ftp_rename_cbs = {
	.userdata = &s_app,
	.complete = &ftp_rename_complete_cb,
};

static void ftp_delete_complete_cb(struct arsdk_ftp_itf *itf,
		struct arsdk_ftp_req_delete *req,
		enum arsdk_ftp_req_status status,
		int error,
		void *userdata)
{
	const char *path = arsdk_ftp_req_delete_get_path(req);

	LOGI("ftp delete %s status: %d", path, status);
}

static struct arsdk_ftp_req_delete_cbs s_ftp_delete_cbs = {
	.userdata = &s_app,
	.complete = &ftp_delete_complete_cb,
};

static void ftp_list_complete(struct arsdk_ftp_itf *itf,
		struct arsdk_ftp_req_list *req,
		enum arsdk_ftp_req_status status,
		int error,
		void *userdata)
{
	struct arsdk_ftp_file_list *list = NULL;
	struct arsdk_ftp_file *prev = NULL;
	const char *path = arsdk_ftp_req_list_get_path(req);

	LOGI("ftp list of %s status: %d", path, status);

	if (status != ARSDK_FTP_REQ_STATUS_OK)
		return;

	list = arsdk_ftp_req_list_get_result(req);
	LOGI("files:");
	do {
		prev = arsdk_ftp_file_list_next_file(list, prev);

		if (prev != NULL) {
			LOGI("\tname %s size: %zu type: %d",
					arsdk_ftp_file_get_name(prev),
					arsdk_ftp_file_get_size(prev),
					arsdk_ftp_file_get_type(prev));
		}
	} while (prev != NULL);
}

static struct arsdk_ftp_req_list_cbs s_ftp_list_cbs = {
	.userdata = &s_app,
	.complete = &ftp_list_complete,
};

static void ftp_run(struct app *app)
{
	int res = 0;
	struct arsdk_ftp_req_get *req_get = NULL;
	struct arsdk_ftp_req_put *req_put = NULL;
	struct arsdk_ftp_req_list *req_list = NULL;
	struct arsdk_ftp_req_rename *req_rename = NULL;
	struct arsdk_ftp_req_delete *req_delete = NULL;

	res = arsdk_device_get_ftp_itf(app->device, &app->ftp.itf);
	if (res < 0) {
		LOG_ERRNO("arsdk_device_get_ftp_itf", -res);
		return;
	}

	/* Run ftp requests */
	if ((app->ftp.get.remote_path != NULL) &&
	    (app->ftp.get.local_path != NULL)) {
		res = arsdk_ftp_itf_create_req_get(app->ftp.itf,
				&s_ftp_get_cbs,
				app->ftp.dev_type,
				ARSDK_FTP_SRV_TYPE_MEDIA,
				app->ftp.get.remote_path,
				app->ftp.get.local_path,
				0,
				&req_get);
		if (res < 0)
			LOG_ERRNO("arsdk_ftp_itf_create_req_get", -res);
	}

	if ((app->ftp.put.remote_path != NULL) &&
	    (app->ftp.put.local_path != NULL)) {
		res = arsdk_ftp_itf_create_req_put(app->ftp.itf, &s_ftp_put_cbs,
				app->ftp.dev_type,
				ARSDK_FTP_SRV_TYPE_MEDIA,
				app->ftp.put.remote_path,
				app->ftp.put.local_path,
				0,
				&req_put);
		if (res < 0)
			LOG_ERRNO("arsdk_ftp_itf_create_req_put", -res);
	}

	if (app->ftp.list.path != NULL) {
		res = arsdk_ftp_itf_create_req_list(app->ftp.itf, &s_ftp_list_cbs,
				app->ftp.dev_type,
				ARSDK_FTP_SRV_TYPE_MEDIA,
				app->ftp.list.path,
				&req_list);
		if (res < 0)
			LOG_ERRNO("arsdk_ftp_itf_create_req_list", -res);
	}

	if ((app->ftp.rename.src != NULL) &&
	    (app->ftp.rename.dst != NULL)) {
		res = arsdk_ftp_itf_create_req_rename(app->ftp.itf, &s_ftp_rename_cbs,
				app->ftp.dev_type,
				ARSDK_FTP_SRV_TYPE_MEDIA,
				app->ftp.rename.src,
				app->ftp.rename.dst,
				&req_rename);
		if (res < 0)
			LOG_ERRNO("arsdk_ftp_itf_create_req_rename", -res);
	}

	if (app->ftp.delete.path != NULL) {
		res = arsdk_ftp_itf_create_req_delete(app->ftp.itf, &s_ftp_delete_cbs,
				app->ftp.dev_type,
				ARSDK_FTP_SRV_TYPE_MEDIA,
				app->ftp.delete.path,
				&req_delete);
		if (res < 0)
			LOG_ERRNO("arsdk_ftp_itf_create_req_delete", -res);
	}
}

static void media_list_complete(struct arsdk_media_itf *itf,
		struct arsdk_media_req_list *req,
		enum arsdk_media_req_status status,
		int error,
		void *userdata)
{
	struct arsdk_media *prev = NULL;
	struct arsdk_media_res *res_prev = NULL;
	struct arsdk_media_list *media_list = NULL;
	const struct tm *date = NULL;
	char str_date[500];

	LOGI("media list status: %d", status);

	if (status != ARSDK_MEDIA_REQ_STATUS_OK)
		return;

	media_list = arsdk_media_req_list_get_result(req);
	if (media_list == NULL)
		return;

	LOGI("medias (%zu) :",
			arsdk_media_list_get_count(media_list));
	prev = arsdk_media_list_next_media(media_list, prev);
	while (prev != NULL) {

		date = arsdk_media_get_date(prev);
		strftime(str_date, sizeof(str_date), "%Y-%m-%dT%H%M%S%z", date);
		LOGI("- media :%p", prev);
		LOGI("  name: %s runid: %s type: %d date: %s",
				arsdk_media_get_name(prev),
				arsdk_media_get_runid(prev),
				arsdk_media_get_type(prev),
				str_date);

		res_prev = arsdk_media_next_res(prev, res_prev);
		while (res_prev != NULL) {

			LOGI("    - uri: %s type: %d fmt :%d size: %zu"
					" name: %s",
					arsdk_media_res_get_uri(res_prev),
					arsdk_media_res_get_type(res_prev),
					arsdk_media_res_get_fmt(res_prev),
					arsdk_media_res_get_size(res_prev),
					arsdk_media_res_get_name(res_prev));

			res_prev = arsdk_media_next_res(prev, res_prev);
		}

		prev = arsdk_media_list_next_media(media_list, prev);
	}
}

static struct arsdk_media_req_list_cbs s_media_list_cbs = {
	.userdata = &s_app,
	.complete = &media_list_complete,
};

static void media_dl_progress(struct arsdk_media_itf *itf,
		struct arsdk_media_req_download *req,
		float percent,
		void *userdata)
{
	LOGI("media dl progress: %f", percent);
}

static void media_dl_complete(struct arsdk_media_itf *itf,
		struct arsdk_media_req_download *req,
		enum arsdk_media_req_status status,
		int error,
		void *userdata)
{
	LOGI("media download complete of uri: %s",
			arsdk_media_req_download_get_uri(req));
	LOGI("status: %d", status);
}

static struct arsdk_media_req_download_cbs s_media_dl_cbs = {
	.userdata = &s_app,
	.progress = &media_dl_progress,
	.complete = &media_dl_complete,
};

static void media_del_complete(struct arsdk_media_itf *itf,
		struct arsdk_media_req_delete *req,
		enum arsdk_media_req_status status,
		int error,
		void *userdata)
{
	struct app *app = userdata;

	LOGI("media delete complete of name: %s", app->media.del.name);
	LOGI("status: %d", status);
}

static struct arsdk_media_req_delete_cbs s_media_del_cbs = {
	.userdata = &s_app,
	.complete = &media_del_complete,
};

static void media_list_for_del_complete(struct arsdk_media_itf *itf,
		struct arsdk_media_req_list *req,
		enum arsdk_media_req_status status,
		int error,
		void *userdata)
{
	struct arsdk_media_list *media_list = NULL;
	struct arsdk_media *prev = NULL;
	const char *media_name = NULL;
	int res = 0;
	struct arsdk_media_req_delete *req_del = NULL;
	struct app *app = userdata;
	enum arsdk_device_type dev_type;

	if (status != ARSDK_MEDIA_REQ_STATUS_OK)
		return;

	media_list = arsdk_media_req_list_get_result(req);
	if (media_list == NULL)
		return;

	dev_type = arsdk_media_req_list_get_dev_type(req);

	prev = arsdk_media_list_next_media(media_list, prev);
	while (prev != NULL) {
		media_name = arsdk_media_get_name(prev);

		if (strcmp(media_name, app->media.del.name) == 0) {
			res = arsdk_media_itf_create_req_delete(app->media.itf,
					&s_media_del_cbs,
					prev,
					dev_type,
					&req_del);
			if (res < 0)
				LOG_ERRNO("arsdk_media_itf_delete", -res);
			return;
		}

		prev = arsdk_media_list_next_media(media_list, prev);
	}

	LOGE("media to delete not found id :%s", app->media.del.name);
}

static struct arsdk_media_req_list_cbs s_media_list_for_del_cbs = {
	.userdata = &s_app,
	.complete = &media_list_for_del_complete,
};

static void media_run(struct app *app)
{
	int res = 0;
	struct arsdk_media_req_list *req_list = NULL;
	struct arsdk_media_req_download *req_dl = NULL;

	res = arsdk_device_get_media_itf(app->device, &app->media.itf);
	if (res < 0) {
		LOG_ERRNO("arsdk_device_get_media_itf", -res);
		return;
	}

	if (app->media.list.used) {
		res = arsdk_media_itf_create_req_list(app->media.itf,
				&s_media_list_cbs,
				app->media.list.types,
				app->media.dev_type,
				&req_list);
		if (res < 0)
			LOG_ERRNO("arsdk_media_itf_list", -res);
	}

	if ((app->media.dl.uri != NULL) &&
	    (app->media.dl.local_path != NULL)) {
		res = arsdk_media_itf_create_req_download(app->media.itf,
				&s_media_dl_cbs,
				app->media.dl.uri,
				app->media.dl.local_path,
				app->media.dev_type,
				0,
				&req_dl);
		if (res < 0)
			LOG_ERRNO("arsdk_media_itf_download", -res);
	}

	if (app->media.del.name != NULL) {
		res = arsdk_media_itf_create_req_list(app->media.itf,
				&s_media_list_for_del_cbs,
				ARSDK_MEDIA_TYPE_ALL,
				app->media.dev_type,
				&req_list);
		if (res < 0)
			LOG_ERRNO("arsdk_media_itf_list_type", -res);
	}
}

static void update_upload_progress(struct arsdk_updater_itf *itf,
		struct arsdk_updater_req_upload *req,
		float percent,
		void *userdata)
{
	LOGI("update upload progress: %f", percent);
}

static void update_upload_complete(struct arsdk_updater_itf *itf,
		struct arsdk_updater_req_upload *req,
		enum arsdk_updater_req_status status,
		int error,
		void *userdata)
{
	LOGI("update upload complete with status: %d", status);
}

static struct arsdk_updater_req_upload_cbs s_update_up_cbs = {
	.userdata = &s_app,
	.progress = &update_upload_progress,
	.complete = &update_upload_complete,
};

static void update_run(struct app *app)
{
	int res = 0;
	struct arsdk_updater_req_upload *req_upload = NULL;

	res = arsdk_device_get_updater_itf(app->device, &app->update.itf);
	if (res < 0) {
		LOG_ERRNO("arsdk_device_get_updater_itf", -res);
		return;
	}

	res = arsdk_updater_itf_create_req_upload(app->update.itf,
			app->update.fw_filepath, app->update.dev_type,
			&s_update_up_cbs,
			&req_upload);
	if (res < 0) {
		LOG_ERRNO("arsdk_updater_itf_create_req_upload", -res);
		return;
	}
}

static void crashml_progress_cb(struct arsdk_crashml_itf *itf,
		struct arsdk_crashml_req *req,
		const char *name,
		int count,
		int total,
		enum arsdk_crashml_req_status status,
		void *userdata)
{
	LOGD("%s name:%s %d/%d status:%d", __func__, name,
			count, total, status);
}

static void crashml_complete_cb(struct arsdk_crashml_itf *itf,
		struct arsdk_crashml_req *req,
		enum arsdk_crashml_req_status status,
		int error,
		void *userdata)
{
	LOGD("%s status:%d", __func__, status);
}

static struct arsdk_crashml_req_cbs s_crashml_cbs = {
	.userdata = &s_app,
	.progress = &crashml_progress_cb,
	.complete = &crashml_complete_cb,
};

static void crashml_run(struct app *app)
{
	int res = 0;
	struct arsdk_crashml_req *req = NULL;

	LOGI("%s", __func__);

	res = arsdk_device_get_crashml_itf(app->device, &app->crashml.itf);
	if (res < 0) {
		LOG_ERRNO("arsdk_device_get_crashml_itf", -res);
		return;
	}

	res = arsdk_crashml_itf_create_req(app->crashml.itf,
			app->crashml.dir_path, app->crashml.dev_type,
			&s_crashml_cbs, app->crashml.types, &req);
	if (res < 0) {
		LOG_ERRNO("arsdk_crashml_itf_start", -res);
		return;
	}
}

static void flight_log_progress_cb(struct arsdk_flight_log_itf *itf,
		struct arsdk_flight_log_req *req,
		const char *name,
		int count,
		int total,
		enum arsdk_flight_log_req_status status,
		void *userdata)
{
	LOGD("%s name:%s %d/%d status:%d", __func__, name,
			count, total, status);
}

static void flight_log_complete_cb(struct arsdk_flight_log_itf *itf,
		struct arsdk_flight_log_req *req,
		enum arsdk_flight_log_req_status status,
		int error,
		void *userdata)
{
	LOGD("%s status:%d", __func__, status);
}

static struct arsdk_flight_log_req_cbs s_flight_log_cbs = {
	.userdata = &s_app,
	.progress = &flight_log_progress_cb,
	.complete = &flight_log_complete_cb,
};

static void flight_log_run(struct app *app)
{
	int res = 0;
	struct arsdk_flight_log_req *req = NULL;

	LOGI("%s", __func__);

	res = arsdk_device_get_flight_log_itf(app->device,
			&app->flight_log.itf);
	if (res < 0) {
		LOG_ERRNO("arsdk_device_get_flight_log_itf", -res);
		return;
	}

	res = arsdk_flight_log_itf_create_req(app->flight_log.itf,
			app->flight_log.dir_path, app->flight_log.dev_type,
			&s_flight_log_cbs, &req);
	if (res < 0) {
		LOG_ERRNO("arsdk_flight_log_itf_create_req", -res);
		return;
	}
}

static void pud_progress_cb(struct arsdk_pud_itf *itf,
		struct arsdk_pud_req *req,
		const char *name,
		int count,
		int total,
		enum arsdk_pud_req_status status,
		void *userdata)
{
	LOGD("%s name:%s %d/%d status:%d", __func__, name,
			count, total, status);
}

static void pud_complete_cb(struct arsdk_pud_itf *itf,
		struct arsdk_pud_req *req,
		enum arsdk_pud_req_status status,
		int error,
		void *userdata)
{
	LOGD("%s status:%d", __func__, status);
}

static struct arsdk_pud_req_cbs s_pud_cbs = {
	.userdata = &s_app,
	.progress = &pud_progress_cb,
	.complete = &pud_complete_cb,
};

static void pud_run(struct app *app)
{
	int res = 0;
	struct arsdk_pud_req *req = NULL;

	LOGI("%s", __func__);

	res = arsdk_device_get_pud_itf(app->device, &app->pud.itf);
	if (res < 0) {
		LOG_ERRNO("arsdk_device_get_pud_itf", -res);
		return;
	}

	res = arsdk_pud_itf_create_req(app->pud.itf,
			app->pud.dir_path, app->pud.dev_type,
			&s_pud_cbs, &req);
	if (res < 0) {
		LOG_ERRNO("arsdk_pud_itf_start", -res);
		return;
	}
}

static void ephemeris_progress_cb(struct arsdk_ephemeris_itf *itf,
		struct arsdk_ephemeris_req_upload *req,
		float percent,
		void *userdata)
{
	LOGD("%s percent:%f%%", __func__, percent);
}

static void ephemeris_complete_cb(struct arsdk_ephemeris_itf *itf,
		struct arsdk_ephemeris_req_upload *req,
		enum arsdk_ephemeris_req_status status,
		int error,
		void *userdata)
{
	LOGD("%s status:%d", __func__, status);
}

static struct arsdk_ephemeris_req_upload_cbs s_ephemeris_cbs = {
	.userdata = &s_app,
	.progress = &ephemeris_progress_cb,
	.complete = &ephemeris_complete_cb,
};

static void ephemeris_run(struct app *app)
{
	int res = 0;
	struct arsdk_ephemeris_req_upload *req = NULL;

	LOGI("%s", __func__);

	res = arsdk_device_get_ephemeris_itf(app->device,
			&app->ephemeris.itf);
	if (res < 0) {
		LOG_ERRNO("arsdk_device_get_ephemeris_itf", -res);
		return;
	}

	res = arsdk_ephemeris_itf_create_req_upload(app->ephemeris.itf,
			app->ephemeris.file_path, app->ephemeris.dev_type,
			&s_ephemeris_cbs, &req);
	if (res < 0) {
		LOG_ERRNO("arsdk_ephemeris_itf_start", -res);
		return;
	}
}

static void blackbox_rc_button_action_cb(struct arsdk_blackbox_itf *itf,
		struct arsdk_blackbox_listener *listener,
		int action,
		void *userdata)
{
	LOGI("%s action:%d", __func__, action);
}

static void blackbox_rc_piloting_info_cb(struct arsdk_blackbox_itf *itf,
		struct arsdk_blackbox_listener *listener,
		struct arsdk_blackbox_rc_piloting_info *info,
		void *userdata)
{
	if (info == NULL)
		return;

	LOGI("%s pitch:%d roll:%d  yaw:%d  gaz:%d  source:%d", __func__,
			info->pitch, info->roll, info->yaw, info->gaz,
			info->source);
}

static void blackbox_unregister(struct arsdk_blackbox_itf *itf,
		struct arsdk_blackbox_listener *listener,
		void *userdata)
{
	struct app *app = userdata;
	app->blackbox.listener = NULL;
	LOGI("blackbox listener unregistered");
}

struct arsdk_blackbox_listener_cbs s_blackbox_listener_cbs = {
	.userdata = &s_app,
	.rc_button_action = &blackbox_rc_button_action_cb,
	.rc_piloting_info = &blackbox_rc_piloting_info_cb,
	.unregister = &blackbox_unregister,
};

static void enable_blackbox(struct app *app)
{
	int res = 0;

	LOGI("%s", __func__);

	/* register blackbox listener */
	res = arsdk_device_get_blackbox_itf(app->device, &app->blackbox.itf);
	if (res < 0) {
		LOG_ERRNO("arsdk_device_get_blackbox_itf", -res);
		return;
	}

	res = arsdk_blackbox_itf_create_listener(app->blackbox.itf,
			&s_blackbox_listener_cbs,
			&app->blackbox.listener);
	if (res < 0)
		LOG_ERRNO("arsdk_blackbox_itf_create_listener", -res);
}

static void tcp_send_stop(struct app *app)
{
	LOGI("%s", __func__);

	if (app->tcp_send.proxy != NULL) {
		arsdk_device_destroy_tcp_proxy(app->tcp_send.proxy);
		app->tcp_send.proxy = NULL;
	}

	if (app->tcp_send.pompctx != NULL) {
		pomp_ctx_stop(app->tcp_send.pompctx);
		pomp_ctx_destroy(app->tcp_send.pompctx);
		app->tcp_send.pompctx = NULL;
	}
}

/**
 */
static void tcp_send_event_cb(struct pomp_ctx *ctx,
		enum pomp_event event,
		struct pomp_conn *conn,
		const struct pomp_msg *msg,
		void *userdata)
{
	int res = 0;
	struct app *app = userdata;
	struct pomp_buffer *buf = NULL;

	LOGI("%s", __func__);

	switch (event) {
	case POMP_EVENT_CONNECTED:
		/* Sent data */
		buf = pomp_buffer_new_with_data(app->tcp_send.data,
					strlen(app->tcp_send.data)+1);
		res = pomp_ctx_send_raw_buf(app->tcp_send.pompctx, buf);
		if (res < 0) {
			LOG_ERRNO("pomp_ctx_send_raw_buf failed", -res);
			pomp_ctx_stop(app->tcp_send.pompctx);
		}
		pomp_buffer_unref(buf);
		buf = NULL;
		break;

	case POMP_EVENT_DISCONNECTED:
		break;

	case POMP_EVENT_MSG:
		/* Never received for raw context */
		break;
	}
}

/**
 */
static void tcp_send_raw_cb(struct pomp_ctx *ctx,
		struct pomp_conn *conn,
		struct pomp_buffer *buf,
		void *userdata)
{
	const char *cdata = NULL;

	pomp_buffer_get_cdata(buf, (const void **)&cdata, NULL, NULL);

	fprintf(stderr, "tcp received data: %s \n", cdata);
}

static void tcp_send_run(struct app *app)
{
	int res = 0;
	int port = 0;
	const char *addrstr = NULL;
	struct sockaddr_in addr;
	socklen_t addrlen = 0;

	LOGI("%s", __func__);

	res = arsdk_device_create_tcp_proxy(app->device,
			app->tcp_send.dev_type,
			app->tcp_send.port,
			&app->tcp_send.proxy);
	if (res < 0) {
		LOG_ERRNO("arsdk_device_create_tcp_proxy", -res);
		return;
	}
	port = arsdk_device_tcp_proxy_get_port(app->tcp_send.proxy);
	addrstr = arsdk_device_tcp_proxy_get_addr(app->tcp_send.proxy);

	LOGI("tcp send to %s:%d data: %s", addrstr, port, app->tcp_send.data);

	/* connect */
	app->tcp_send.pompctx = pomp_ctx_new_with_loop(
				&tcp_send_event_cb, app, app->loop);
	pomp_ctx_set_raw(app->tcp_send.pompctx, &tcp_send_raw_cb);

	/* Setup address */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(addrstr);
	addrlen = sizeof(addr);

	/* Start connecting */
	res = pomp_ctx_connect(app->tcp_send.pompctx,
			(const struct sockaddr *)&addr, addrlen);
	if (res < 0)
		LOG_ERRNO("pomp_ctx_connect", -res);
}

/**
 */
static void send_status(struct arsdk_cmd_itf *itf,
		const struct arsdk_cmd *cmd,
		enum arsdk_cmd_itf_send_status status,
		int done,
		void *userdata)
{
	LOGI("cmd %u,%u,%u: %s%s", cmd->prj_id, cmd->cls_id, cmd->cmd_id,
			arsdk_cmd_itf_send_status_str(status),
			done ? "(DONE)" : "");
}

/**
 */
static void link_quality(struct arsdk_cmd_itf *itf,
		int32_t tx_quality,
		int32_t rx_quality,
		int32_t rx_useful,
		void *userdata)
{
	LOGI("link_quality tx_quality:%d%% rx_quality:%d%% rx_useful:%d%%",
			tx_quality,
			rx_quality,
			rx_useful);
}

static void subDeviceConnectionChanged(struct app *app,
		const struct arsdk_cmd *cmd)
{
	int res = 0;
	const struct arsdk_device_info *dev_info = NULL;
	int32_t status = 0;
	const char *name = NULL;
	uint16_t productID = 0;

	res = arsdk_cmd_dec_Skyctrl_DeviceState_ConnexionChanged(cmd, &status,
			&name, &productID);
	if (res < 0) {
		LOG_ERRNO("arsdk_cmd_dec_Skyctrl_DeviceState_ConnexionChanged",
				-res);
		return;
	}

	res = arsdk_device_get_info(app->device, &dev_info);
	if (res < 0) {
		LOG_ERRNO("arsdk_device_get_info", -res);
		return;
	}

	switch (status) {
	case ARSDK_SKYCTRL_DEVICESTATE_CONNEXIONCHANGED_STATUS_CONNECTED:
		/* Run sub device ftp requests */
		if ((app->ftp.used) &&
		    (app->ftp.dev_type != dev_info->type)) {
			app->ftp.dev_type = (enum arsdk_device_type)productID;
			ftp_run(app);
		}

		/* Run sub device media requests */
		if (app->media.used) {
			app->media.dev_type = (enum arsdk_device_type)productID;
			media_run(app);
		}

		/* Run Update */
		if ((app->update.used) &&
		    (app->update.dev_type != dev_info->type)) {
			app->update.dev_type =
					(enum arsdk_device_type)productID;
			update_run(app);
		}

		/* Run Crashml */
		if ((app->crashml.used) &&
		    (app->crashml.dev_type != dev_info->type)) {
			app->crashml.dev_type =
					(enum arsdk_device_type)productID;
			crashml_run(app);
		}

		/* Run Flight log */
		if ((app->flight_log.used) &&
		    (app->flight_log.dev_type != dev_info->type)) {
			app->flight_log.dev_type =
					(enum arsdk_device_type)productID;
			flight_log_run(app);
		}

		/* Run Pud */
		if ((app->pud.used) &&
		    (app->pud.dev_type != dev_info->type)) {
			app->pud.dev_type = (enum arsdk_device_type)productID;
			pud_run(app);
		}

		/* Run ephemeris */
		if ((app->ephemeris.used) &&
		    (app->ephemeris.dev_type != dev_info->type)) {
			app->ephemeris.dev_type =
					(enum arsdk_device_type)productID;
			ephemeris_run(app);
		}

		/* Run tcp send */
		if ((app->tcp_send.used) &&
		    (app->tcp_send.dev_type != dev_info->type)) {
			app->tcp_send.dev_type =
					(enum arsdk_device_type)productID;
			tcp_send_run(app);
		}

		break;
	case ARSDK_SKYCTRL_DEVICESTATE_CONNEXIONCHANGED_STATUS_DISCONNECTING:
		break;
	default:
		break;
	}
}

static void send_start_video(struct app *app)
{
	/* Send 'MediaStreaming' command */

	int res = 0;
	const struct arsdk_device_info *dev_info = NULL;

	res = arsdk_device_get_info(app->device, &dev_info);
	if (res < 0) {
		LOG_ERRNO("arsdk_device_get_info", -res);
		return;
	}

	switch (dev_info->type) {
	case ARSDK_DEVICE_TYPE_BEBOP :
	case ARSDK_DEVICE_TYPE_BEBOP_2 :
	case ARSDK_DEVICE_TYPE_PAROS :
	case ARSDK_DEVICE_TYPE_ANAFI4K :
	case ARSDK_DEVICE_TYPE_ANAFI_THERMAL :
	case ARSDK_DEVICE_TYPE_CHIMERA :
	case ARSDK_DEVICE_TYPE_SKYCTRL :
	case ARSDK_DEVICE_TYPE_SKYCTRL_NG :
	case ARSDK_DEVICE_TYPE_SKYCTRL_2 :
	case ARSDK_DEVICE_TYPE_SKYCTRL_2P :
	case ARSDK_DEVICE_TYPE_SKYCTRL_3 :
	case ARSDK_DEVICE_TYPE_SKYCTRL_UA :
	case ARSDK_DEVICE_TYPE_EVINRUDE :
		res = arsdk_cmd_send_Ardrone3_MediaStreaming_VideoEnable(
				app->cmd_itf, &send_status, app, 1);
		if (res < 0)
			LOG_ERRNO("arsdk_cmd_enc", -res);
		break;
	case ARSDK_DEVICE_TYPE_JS :
	case ARSDK_DEVICE_TYPE_JS_EVO_LIGHT :
	case ARSDK_DEVICE_TYPE_JS_EVO_RACE :
	case ARSDK_DEVICE_TYPE_POWERUP :
		res = arsdk_cmd_send_Jpsumo_MediaStreaming_VideoEnable(
				app->cmd_itf, &send_status, app, 1);
		if (res < 0)
			LOG_ERRNO("arsdk_cmd_enc", -res);
		break;

	case ARSDK_DEVICE_TYPE_RS :
	case ARSDK_DEVICE_TYPE_RS_EVO_LIGHT :
	case ARSDK_DEVICE_TYPE_RS_EVO_BRICK :
	case ARSDK_DEVICE_TYPE_RS_EVO_HYDROFOIL :
	case ARSDK_DEVICE_TYPE_RS3 :
	case ARSDK_DEVICE_TYPE_WINGX :
	default:
		break;
	}
}

/**
 */
static void recv_cmd(struct arsdk_cmd_itf *itf,
		const struct arsdk_cmd *cmd,
		void *userdata)
{
	struct app *app = userdata;
	char buf[512] = "";
	LOGI("%s", __func__);

	/* Format and log received command */
	arsdk_cmd_fmt(cmd, buf, sizeof(buf));
	LOGI("%s", buf);

	switch (cmd->id) {
	case ARSDK_ID_COMMON_SETTINGSSTATE_ALLSETTINGSCHANGED:
		send_start_video(app);
		break;
	case ARSDK_ID_SKYCTRL_DEVICESTATE_CONNEXIONCHANGED:
		subDeviceConnectionChanged(app, cmd);
		break;
	default:
		break;
	}
}

/**
 */
static void timer_cb(struct pomp_timer *timer, void *userdata)
{
	int res = 0;
	struct app *app = userdata;
	struct arsdk_cmd cmd;
	const struct arsdk_device_info *dev_info = NULL;

	res = arsdk_device_get_info(app->device, &dev_info);
	if (res < 0) {
		LOG_ERRNO("arsdk_device_get_info", -res);
		return;
	}

	/* Create 'PCMD' command */
	arsdk_cmd_init(&cmd);

	switch (dev_info->type) {
	case ARSDK_DEVICE_TYPE_BEBOP :
	case ARSDK_DEVICE_TYPE_BEBOP_2 :
	case ARSDK_DEVICE_TYPE_PAROS :
	case ARSDK_DEVICE_TYPE_ANAFI4K :
	case ARSDK_DEVICE_TYPE_ANAFI_THERMAL :
	case ARSDK_DEVICE_TYPE_CHIMERA :
	case ARSDK_DEVICE_TYPE_SKYCTRL :
	case ARSDK_DEVICE_TYPE_SKYCTRL_NG :
	case ARSDK_DEVICE_TYPE_SKYCTRL_2 :
	case ARSDK_DEVICE_TYPE_SKYCTRL_2P :
	case ARSDK_DEVICE_TYPE_SKYCTRL_3 :
	case ARSDK_DEVICE_TYPE_SKYCTRL_UA :
	case ARSDK_DEVICE_TYPE_EVINRUDE :
		res = arsdk_cmd_enc_Ardrone3_Piloting_PCMD(&cmd,
				0 /*_flag*/,
				0 /*_roll*/,
				0 /*_pitch*/,
				0 /*_yaw*/,
				0 /*_gaz*/,
				0 /*_timestampAndSeqNum*/);
		if (res < 0) {
			LOG_ERRNO("arsdk_cmd_enc", -res);
			goto end;
		}
		break;
	case ARSDK_DEVICE_TYPE_JS :
	case ARSDK_DEVICE_TYPE_JS_EVO_LIGHT :
	case ARSDK_DEVICE_TYPE_JS_EVO_RACE :
	case ARSDK_DEVICE_TYPE_POWERUP :
		res = arsdk_cmd_enc_Jpsumo_Piloting_PCMD(&cmd,
				0 /*_flag*/,
				0 /*_spped*/,
				0 /*_turn*/);
		if (res < 0) {
			LOG_ERRNO("arsdk_cmd_enc", -res);
			goto end;
		}
		break;

	case ARSDK_DEVICE_TYPE_RS :
	case ARSDK_DEVICE_TYPE_RS_EVO_LIGHT :
	case ARSDK_DEVICE_TYPE_RS_EVO_BRICK :
	case ARSDK_DEVICE_TYPE_RS_EVO_HYDROFOIL :
	case ARSDK_DEVICE_TYPE_RS3 :
	case ARSDK_DEVICE_TYPE_WINGX :
	default:
		goto end;
		break;
	}

	/* Send command */
	res = arsdk_cmd_itf_send(app->cmd_itf, &cmd, &send_status, app);
	if (res < 0)
		LOG_ERRNO("arsdk_cmd_itf_send", -res);

end:
	/* Cleanup */
	arsdk_cmd_clear(&cmd);
}

/**
 */
static void connecting(struct arsdk_device *device,
		const struct arsdk_device_info *info,
		void *userdata)
{
	LOGI("%s", __func__);
}

static int isRadioControl(const struct arsdk_device_info *dev_info)
{
	if ((dev_info->type == ARSDK_DEVICE_TYPE_SKYCTRL) ||
	    (dev_info->type == ARSDK_DEVICE_TYPE_SKYCTRL_2) ||
	    (dev_info->type == ARSDK_DEVICE_TYPE_SKYCTRL_2P) ||
	    (dev_info->type == ARSDK_DEVICE_TYPE_SKYCTRL_NG) ||
	    (dev_info->type == ARSDK_DEVICE_TYPE_SKYCTRL_3) ||
	    (dev_info->type == ARSDK_DEVICE_TYPE_SKYCTRL_UA))
		return 1;
	else
		return 0;
}

static void sendInitCmds(struct app *app,
		const struct arsdk_device_info *dev_info)
{
	int res = 0;

	if (isRadioControl(dev_info)) {
		res = arsdk_cmd_send_Skyctrl_Settings_AllSettings(
				app->cmd_itf, &send_status, app);
		if (res < 0)
			LOG_ERRNO("arsdk_cmd_enc", -res);

		res = arsdk_cmd_send_Skyctrl_Common_AllStates(
				app->cmd_itf, &send_status, app);
		if (res < 0)
			LOG_ERRNO("arsdk_cmd_enc", -res);
	} else {
		res = arsdk_cmd_send_Common_Settings_AllSettings(
				app->cmd_itf, &send_status, app);
		if (res < 0)
			LOG_ERRNO("arsdk_cmd_enc", -res);

		res = arsdk_cmd_send_Common_Common_AllStates(
				app->cmd_itf, &send_status, app);
		if (res < 0)
			LOG_ERRNO("arsdk_cmd_enc", -res);
	}
}

static uint32_t get_pcmd_period(const struct arsdk_device_info *info)
{
	switch (info->type) {
	case ARSDK_DEVICE_TYPE_BEBOP :
	case ARSDK_DEVICE_TYPE_BEBOP_2 :
	case ARSDK_DEVICE_TYPE_PAROS :
	case ARSDK_DEVICE_TYPE_ANAFI4K :
	case ARSDK_DEVICE_TYPE_ANAFI_THERMAL :
	case ARSDK_DEVICE_TYPE_CHIMERA :
	case ARSDK_DEVICE_TYPE_SKYCTRL :
	case ARSDK_DEVICE_TYPE_SKYCTRL_NG :
	case ARSDK_DEVICE_TYPE_SKYCTRL_2 :
	case ARSDK_DEVICE_TYPE_SKYCTRL_2P :
	case ARSDK_DEVICE_TYPE_SKYCTRL_3 :
	case ARSDK_DEVICE_TYPE_SKYCTRL_UA :
	case ARSDK_DEVICE_TYPE_EVINRUDE :
		return PCMD_PERIOD_DRONE;
	case ARSDK_DEVICE_TYPE_JS :
	case ARSDK_DEVICE_TYPE_JS_EVO_LIGHT :
	case ARSDK_DEVICE_TYPE_JS_EVO_RACE :
	case ARSDK_DEVICE_TYPE_POWERUP :
		return PCMD_PERIOD_JS;

	case ARSDK_DEVICE_TYPE_RS :
	case ARSDK_DEVICE_TYPE_RS_EVO_LIGHT :
	case ARSDK_DEVICE_TYPE_RS_EVO_BRICK :
	case ARSDK_DEVICE_TYPE_RS_EVO_HYDROFOIL :
	case ARSDK_DEVICE_TYPE_RS3 :
	case ARSDK_DEVICE_TYPE_WINGX :
	default:
		return 0;
	}
}

/**
 */
static void connected(struct arsdk_device *device,
		const struct arsdk_device_info *info,
		void *userdata)
{
	int res = 0;
	struct app *app = userdata;
	struct arsdk_cmd_itf_cbs cmd_cbs;
	uint32_t pcmd_period = 0;
	LOGI("%s", __func__);

	/* Create command interface object */
	memset(&cmd_cbs, 0, sizeof(cmd_cbs));
	cmd_cbs.userdata = app;
	cmd_cbs.recv_cmd = &recv_cmd;
	cmd_cbs.send_status = &send_status;
	cmd_cbs.link_quality = &link_quality;
	res = arsdk_device_create_cmd_itf(device, &cmd_cbs, &app->cmd_itf);
	if (res < 0)
		LOG_ERRNO("arsdk_device_create_cmd_itf", -res);

	/* Setup periodic timer to send 'PCMD' commands */
	pcmd_period = get_pcmd_period(info);
	res = pomp_timer_set_periodic(app->timer, pcmd_period, pcmd_period);
	if (res < 0)
		LOG_ERRNO("pomp_timer_set", -res);

	/* Send initializing commands */
	sendInitCmds(app, info);

	/* Run direct ftp requests */
	if ((app->ftp.used) &&
	    ((app->ftp.dev_type == 0) || (app->ftp.dev_type == info->type))) {
		app->ftp.dev_type = info->type;
		ftp_run(app);
	}

	/* Run direct media requests */
	if ((app->media.used) && (!isRadioControl(info))) {
		app->media.dev_type = info->type;
		media_run(app);
	}

	/* Run Update */
	if ((app->update.used) &&
	    ((app->update.dev_type == 0) ||
	     (app->update.dev_type == info->type))) {
		app->update.dev_type = info->type;
		update_run(app);
	}

	/* Run Crashml */
	if ((app->crashml.used) &&
	    ((app->crashml.dev_type == 0) ||
	     (app->crashml.dev_type == info->type))) {
		app->crashml.dev_type = info->type;
		crashml_run(app);
	}

	/* Run Flight Log */
	if ((app->flight_log.used) &&
	    ((app->flight_log.dev_type == 0) ||
	     (app->flight_log.dev_type == info->type))) {
		app->flight_log.dev_type = info->type;
		flight_log_run(app);
	}

	/* Run Pud */
	if ((app->pud.used) &&
	    ((app->pud.dev_type == 0) || (app->pud.dev_type == info->type))) {
		app->pud.dev_type = info->type;
		pud_run(app);
	}

	/* Run Ephemeris */
	if ((app->ephemeris.used) &&
	    ((app->ephemeris.dev_type == 0) ||
	     (app->ephemeris.dev_type == info->type))) {
		app->ephemeris.dev_type = info->type;
		ephemeris_run(app);
	}

	/* Enable blackbox */
	if (app->blackbox.used)
		enable_blackbox(app);

	/* Run tcp send */
	if ((app->tcp_send.used) &&
	    ((app->tcp_send.dev_type == 0) ||
	     (app->tcp_send.dev_type == info->type))) {
		app->tcp_send.dev_type = info->type;
		tcp_send_run(app);
	}
}

/**
 */
static void disconnected(struct arsdk_device *device,
		const struct arsdk_device_info *info,
		void *userdata)
{
	int res = 0;
	struct app *app = userdata;
	LOGI("%s", __func__);

	res = pomp_timer_clear(app->timer);
	if (res < 0)
		LOG_ERRNO("pomp_timer_clear", -res);

	app->device = NULL;
}

/**
 */
static void canceled(struct arsdk_device *device,
		const struct arsdk_device_info *info,
		enum arsdk_conn_cancel_reason reason,
		void *userdata)
{
	struct app *app = userdata;
	LOGI("%s: reason=%s", __func__, arsdk_conn_cancel_reason_str(reason));
	app->device = NULL;
}

/**
 */
static void link_status(struct arsdk_device *device,
		const struct arsdk_device_info *info,
		enum arsdk_link_status status,
		void *userdata)
{
	LOGI("%s: status=%s", __func__, arsdk_link_status_str(status));
	if (status == ARSDK_LINK_STATUS_KO)
		arsdk_device_disconnect(device);
}

/**
 */
static void device_added(struct arsdk_device *device, void *userdata)
{
	int res = 0;
	struct app *app = userdata;
	struct arsdk_device_conn_cfg cfg;
	struct arsdk_device_conn_cbs cbs;
	LOGI("%s", __func__);

	/* Only interested in first device found */
	if (app->device != NULL)
		return;

	/* Save device */
	app->device = device;

	/* Connect to device */
	memset(&cfg, 0, sizeof(cfg));
	cfg.ctrl_name = "controller";
	cfg.ctrl_type = "test";

	memset(&cbs, 0, sizeof(cbs));
	cbs.userdata = app;
	cbs.connecting = &connecting;
	cbs.connected = &connected;
	cbs.disconnected = &disconnected;
	cbs.canceled = &canceled;
	cbs.link_status = &link_status;

	res = arsdk_device_connect(device, &cfg, &cbs, app->loop);
	if (res < 0)
		LOG_ERRNO("arsdk_device_connect", -res);
}

/**
 */
static void device_removed(struct arsdk_device *device, void *userdata)
{
	LOGI("%s", __func__);
}

/**
 */
static void socket_cb(struct arsdkctrl_backend_net *self, int fd,
		enum arsdk_socket_kind kind, void *userdata)
{
	LOGI("socket_cb :self:%p fd:%d kind:%s userdata:%p",
			self, fd, arsdk_socket_kind_str(kind), userdata);
}

/**
 */
static void backend_create(struct app *app)
{
	int res = 0;
	struct arsdkctrl_backend_net_cfg backend_net_cfg;
	struct arsdkctrl_backend_mux_cfg backend_mux_cfg;
	struct arsdk_discovery_cfg discovery_cfg;
	static const enum arsdk_device_type types[] = {
		ARSDK_DEVICE_TYPE_ANAFI4K,
		ARSDK_DEVICE_TYPE_ANAFI_THERMAL,
		ARSDK_DEVICE_TYPE_BEBOP,
		ARSDK_DEVICE_TYPE_BEBOP_2,
		ARSDK_DEVICE_TYPE_EVINRUDE,
		ARSDK_DEVICE_TYPE_JS,
		ARSDK_DEVICE_TYPE_JS_EVO_LIGHT,
		ARSDK_DEVICE_TYPE_JS_EVO_RACE,
		ARSDK_DEVICE_TYPE_SKYCTRL,
		ARSDK_DEVICE_TYPE_SKYCTRL_2,
		ARSDK_DEVICE_TYPE_SKYCTRL_2P,
		ARSDK_DEVICE_TYPE_SKYCTRL_NG,
		ARSDK_DEVICE_TYPE_SKYCTRL_3,
		ARSDK_DEVICE_TYPE_SKYCTRL_UA,
	};

	memset(&discovery_cfg, 0, sizeof(discovery_cfg));
	discovery_cfg.types = types;
	discovery_cfg.count = sizeof(types) / sizeof(types[0]);

	switch (app->backend_type) {
	case ARSDK_BACKEND_TYPE_NET:
		memset(&backend_net_cfg, 0, sizeof(backend_net_cfg));
		backend_net_cfg.stream_supported = 1;
		res = arsdkctrl_backend_net_new(app->ctrl, &backend_net_cfg,
				&app->backend_net);
		if (res < 0)
			LOG_ERRNO("arsdkctrl_backend_net_new", -res);

		res = arsdkctrl_backend_net_set_socket_cb(app->backend_net,
				&socket_cb, &s_app);
		if (res < 0)
			LOG_ERRNO("arsdkctrl_backend_net_set_socket_cb", -res);

		if (app->use_discovery_avahi) {
			/* start avahi discovery */
			res = arsdk_discovery_avahi_new(app->ctrl,
					app->backend_net, &discovery_cfg,
					&app->discovery_avahi);
			if (res < 0)
				LOG_ERRNO("arsdk_discovery_avahi_new", -res);

			res = arsdk_discovery_avahi_start(app->discovery_avahi);
			if (res < 0)
				LOG_ERRNO("arsdk_discovery_avahi_start", -res);
		}

		if (app->use_discovery_net) {
			/* start net discovery */
			res = arsdk_discovery_net_new(app->ctrl,
					app->backend_net, &discovery_cfg,
					s_app.net_device_ip,
					&app->discovery_net);
			if (res < 0)
				LOG_ERRNO("arsdk_discovery_net_new", -res);

			res = arsdk_discovery_net_start(app->discovery_net);
			if (res < 0)
				LOG_ERRNO("arsdk_discovery_net_start", -res);
		}

		break;

	case ARSDK_BACKEND_TYPE_MUX:
		memset(&backend_mux_cfg, 0, sizeof(backend_mux_cfg));
		backend_mux_cfg.mux = app->mux.muxctx;
		res = arsdkctrl_backend_mux_new(app->ctrl, &backend_mux_cfg,
				&app->backend_mux);
		if (res < 0)
			LOG_ERRNO("arsdkctrl_backend_mux_new", -res);

		/* start mux discovery */
		res = arsdk_discovery_mux_new(app->ctrl, app->backend_mux,
				&discovery_cfg, app->mux.muxctx,
				&app->discovery_mux);
		if (res < 0)
			LOG_ERRNO("arsdk_discovery_mux_new", -res);

		res = arsdk_discovery_mux_start(app->discovery_mux);
		if (res < 0)
			LOG_ERRNO("arsdk_discovery_mux_start", -res);
		break;

	case ARSDK_BACKEND_TYPE_BLE: /* NO BREAK */
	case ARSDK_BACKEND_TYPE_UNKNOWN: /* NO BREAK */
	default:
		LOGW("Unsupported backend: %s",
				arsdk_backend_type_str(app->backend_type));
		return;
	}
}

/**
 */
static void backend_destroy(struct app *app)
{
	int res = 0;

	if (app->discovery_avahi) {
		res = arsdk_discovery_avahi_stop(app->discovery_avahi);
		if (res < 0)
			LOG_ERRNO("arsdk_discovery_avahi_stop", -res);

		arsdk_discovery_avahi_destroy(app->discovery_avahi);
		app->discovery_avahi = NULL;
	}

	if (app->discovery_net) {
		res = arsdk_discovery_net_stop(app->discovery_net);
		if (res < 0)
			LOG_ERRNO("arsdk_discovery_net_stop", -res);

		arsdk_discovery_net_destroy(app->discovery_net);
		app->discovery_net = NULL;
	}

	if (app->discovery_mux) {
		res = arsdk_discovery_mux_stop(app->discovery_mux);
		if (res < 0)
			LOG_ERRNO("arsdk_discovery_mux_stop", -res);

		arsdk_discovery_mux_destroy(app->discovery_mux);
		app->discovery_mux = NULL;
	}

	if (app->device != NULL) {
		res = arsdk_device_disconnect(app->device);
		if (res < 0)
			LOG_ERRNO("arsdk_device_disconnect", -res);
		if (app->device != NULL)
			LOGE("s_app.device should be NULL");
		app->device = NULL;
	}

	if (app->backend_mux != NULL) {
		res = arsdkctrl_backend_mux_destroy(app->backend_mux);
		if (res < 0)
			LOG_ERRNO("arsdkctrl_backend_mux_destroy", -res);

		app->backend_mux = NULL;
	}

	if (app->backend_net != NULL) {
		res = arsdkctrl_backend_net_destroy(app->backend_net);
		if (res < 0)
			LOG_ERRNO("arsdkctrl_backend_net_destroy", -res);

		app->backend_net = NULL;
	}
}

/**
 */
static int mux_client_tx(struct mux_ctx *ctx, struct pomp_buffer *buf,
		void *userdata)
{
	struct app *app = userdata;
	return pomp_ctx_send_raw_buf(app->mux.pompctx, buf);
}

/**
 */
static void mux_client_event_cb(struct pomp_ctx *ctx,
		enum pomp_event event,
		struct pomp_conn *conn,
		const struct pomp_msg *msg,
		void *userdata)
{
	int res = 0;
	struct app *app = userdata;
	struct mux_ops ops;

	switch (event) {
	case POMP_EVENT_CONNECTED:
		memset(&ops, 0, sizeof(ops));
		ops.userdata = app;
		ops.tx = &mux_client_tx;
		app->mux.muxctx = mux_new(-1, app->loop, &ops, 0);
		backend_create(app);
		break;

	case POMP_EVENT_DISCONNECTED:
		res = mux_stop(app->mux.muxctx);
		if (res < 0)
			LOG_ERRNO("mux_stop", -res);
		backend_destroy(app);
		mux_unref(app->mux.muxctx);
		app->mux.muxctx = NULL;
		break;

	case POMP_EVENT_MSG:
		/* Never received for raw context */
		break;
	}
}

/**
 */
static void mux_client_raw_cb(struct pomp_ctx *ctx,
		struct pomp_conn *conn,
		struct pomp_buffer *buf,
		void *userdata)
{
	struct app *app = userdata;

	mux_decode(app->mux.muxctx, buf);
}
/**
 */
static void mux_client_create(struct app *app)
{
	int res = 0;
	struct sockaddr_in addr;
	socklen_t addrlen = 0;

	app->mux.pompctx = pomp_ctx_new_with_loop(
			&mux_client_event_cb, app, app->loop);
	pomp_ctx_set_raw(app->mux.pompctx, &mux_client_raw_cb);

	/* Setup address */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(app->mux.port);
	if (app->mux.bridge_ip != NULL)
		addr.sin_addr.s_addr = inet_addr(app->mux.bridge_ip);
	else
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	addrlen = sizeof(addr);

	/* Start connecting */
	res = pomp_ctx_connect(app->mux.pompctx,
			(const struct sockaddr *)&addr, addrlen);
	if (res < 0)
		LOG_ERRNO("pomp_ctx_connect", -res);
}

/**
 */
static void mux_client_destroy(struct app *app)
{
	if (app->mux.pompctx != NULL) {
		pomp_ctx_stop(app->mux.pompctx);
		pomp_ctx_destroy(app->mux.pompctx);
		app->mux.pompctx = NULL;
	}
}

/**
 */
static void sig_handler(int signum)
{
	LOGI("signal %d(%s) received", signum, strsignal(signum));
	s_app.stopped = 1;
	if (s_app.loop != NULL)
		pomp_loop_wakeup(s_app.loop);
}

/**
 */
static void usage(const char *progname)
{
	fprintf(stderr, "usage: %s [<options>]\n", progname);
	fprintf(stderr, "  -h --help   : print this help message and exit\n"
		"  --discovery-avahi (set by default)\n"
		"  --no-discovery-avahi\n"
		"  --discovery-net <device_ip>\n"
		"  --no-discovery-net\n"
		"  --mux\n"
		"  --mux-bridge <bridge_ip> <bridge_port>\n"
		"  --ftp-get <remote_path> <local_path>\n"
		"  --ftp-put <local_path> <remote_path>\n"
		"  --ftp-delete <path>\n"
		"  --ftp-list <path>\n"
		"  --ftp-rename <src> <dst>\n"
		"  --ftp-dev-type <device type>\n"
		"  --media-list [photo ; video]\n"
		"  --media-dl <uri> <local_path>\n"
		"  --media-delete <uri>\n"
		"  --update <firmware file path>\n"
		"  --update-dev-type <device type>\n"
		"  --update-cmp <firmware 1> <firmware 2>\n"
		"  --crashml-dl <local directory>\n"
		"  --crashml-dev-type <device type>\n"
		"  --crashml-type <dir ; tgz>\n"
		"  --flight-log-dl <local directory>\n"
		"  --flight-log-dev-type <device type>\n"
		"  --pud-dl <local directory>\n"
		"  --pud-dev-type <device type>\n"
		"  --ephemeris <local file>\n"
		"  --enable-blackbox\n"
		"  --tcp-send <port> <data>\n"
		"  --tcp-send-dev-type <device type>\n");
}

static int parse_media_list_param(int argc, char *argv[], int *argidx)
{
	(*argidx)++;
	if ((*argidx < argc) && (argv[*argidx][0] != '-')) {
		if (strcmp("photo", argv[*argidx]) == 0) {
			s_app.media.list.types =
					ARSDK_MEDIA_TYPE_PHOTO;
		} else if (strcmp("video", argv[*argidx]) == 0) {
			s_app.media.list.types =
					ARSDK_MEDIA_TYPE_VIDEO;
		} else {
			fprintf(stderr, "Bad media list filter.\n");
			return -EINVAL;
		}
	}
	s_app.media.list.used = 1;
	s_app.media.used = 1;

	return 0;
}

#ifdef BUILD_LIBPUF
static int get_version_info(const char *path, struct puf_version *pv,
		char *name, size_t name_size)
{
	struct puf *puf = NULL;
	int res = 0;

	if ((path == NULL) || (pv == NULL) || (name == NULL))
		return -EINVAL;

	puf = puf_new(path);
	if (puf == NULL) {
		LOGE("puf_new failed");
	}

	/* get version */
	res = puf_get_version(puf, pv);
	if (res < 0) {
		LOG_ERRNO("puf_get_version", -res);
		goto end;
	}

	/* get version name */
	res = puf_version_tostring(pv, name, name_size);
	if (res < 0) {
		LOG_ERRNO("puf_version_tostring", -res);
		goto end;
	}

end:
	puf_destroy(puf);

	return res;
}
#endif /* BUILD_LIBPUF*/

static void run_simple_tools(struct app *app)
{
#ifdef BUILD_LIBPUF
	int res = 0;
	struct puf_version v1;
	char v1_name[50];
	struct puf_version v2;
	char v2_name[50];
	char diff = '=';

	/* cmp fw version */
	if ((app->update.fw2_filepath != NULL) &&
	    (app->update.fw_filepath != NULL)) {

		res = get_version_info(app->update.fw_filepath, &v1,
				v1_name, sizeof(v1_name));
		if (res < 0)
			return;

		res = get_version_info(app->update.fw2_filepath, &v2,
				v2_name, sizeof(v2_name));
		if (res < 0)
			return;

		res = puf_compare_version(&v1, &v2);
		if (res != 0)
			diff = (res > 0) ? '>' : '<';

		fprintf(stderr, "%s(%s) %c %s(%s)\n",
				app->update.fw_filepath,
				v1_name,
				diff,
				app->update.fw2_filepath,
				v2_name);
	}
#endif /* BUILD_LIBPUF*/
}

/**
 */
int main(int argc, char *argv[])
{
	int res = 0;
	int argidx = 0;
	struct arsdk_ctrl_device_cbs ctrl_device_cbs;

	/* Setup signal handlers */
	signal(SIGINT, &sig_handler);
	signal(SIGTERM, &sig_handler);
	signal(SIGPIPE, SIG_IGN);

	s_app.backend_type = ARSDK_BACKEND_TYPE_NET;
	for (argidx = 1; argidx < argc; argidx++) {
		if (argv[argidx][0] != '-') {
			/* End of options */
			break;
		} else if (strcmp(argv[argidx], "-h") == 0
				|| strcmp(argv[argidx], "--help") == 0) {
			/* Help */
			usage(argv[0]);
			return 0;
		} else if (strcmp(argv[argidx], "--discovery-avahi") == 0) {
			s_app.backend_type = ARSDK_BACKEND_TYPE_NET;
			s_app.use_discovery_avahi = 1;
		} else if (strcmp(argv[argidx], "--no-discovery-avahi") == 0) {
			s_app.use_discovery_avahi = 0;
		} else if (strcmp(argv[argidx], "--mux") == 0) {
			s_app.backend_type = ARSDK_BACKEND_TYPE_MUX;
		} else if (strcmp(argv[argidx], "--mux-bridge") == 0) {
			s_app.backend_type = ARSDK_BACKEND_TYPE_MUX;

			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing mux bridge "
						"ip address\n");
				usage(argv[0]);
				return -1;
			}
			s_app.mux.bridge_ip = argv[argidx];

			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing mux bridge port\n");
				usage(argv[0]);
				return -1;
			}
			s_app.mux.port = atoi(argv[argidx]);
		} else if (strcmp(argv[argidx], "--discovery-net") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing discovery net "
						"ip address\n");
				usage(argv[0]);
				return -1;
			}
			/* parse ip device (net discovery)*/
			s_app.net_device_ip = argv[argidx];
			s_app.use_discovery_net = 1;
			s_app.backend_type = ARSDK_BACKEND_TYPE_NET;
		} else if (strcmp(argv[argidx], "--no-discovery-net") == 0) {
			s_app.use_discovery_net = 0;
		} else if (strcmp(argv[argidx], "--ftp-get") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing remote path\n");
				usage(argv[0]);
				return -1;
			}
			s_app.ftp.get.remote_path = argv[argidx];

			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing local path\n");
				usage(argv[0]);
				return -1;
			}
			s_app.ftp.get.local_path = argv[argidx];
			s_app.ftp.used = 1;
		} else if (strcmp(argv[argidx], "--ftp-put") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing local path\n");
				usage(argv[0]);
				return -1;
			}
			s_app.ftp.put.local_path = argv[argidx];

			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing remote path\n");
				usage(argv[0]);
				return -1;
			}
			s_app.ftp.put.remote_path = argv[argidx];
			s_app.ftp.used = 1;
		} else if (strcmp(argv[argidx], "--ftp-delete") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing path\n");
				usage(argv[0]);
				return -1;
			}
			s_app.ftp.delete.path = argv[argidx];
			s_app.ftp.used = 1;
		} else if (strcmp(argv[argidx], "--ftp-list") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing path\n");
				usage(argv[0]);
				return -1;
			}
			s_app.ftp.list.path = argv[argidx];
			s_app.ftp.used = 1;
		} else if (strcmp(argv[argidx], "--ftp-rename") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing source\n");
				usage(argv[0]);
				return -1;
			}
			s_app.ftp.rename.src = argv[argidx];

			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing destination\n");
				usage(argv[0]);
				return -1;
			}
			s_app.ftp.rename.dst = argv[argidx];
			s_app.ftp.used = 1;
		} else if (strcmp(argv[argidx], "--ftp-dev-type") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing device type\n");
				usage(argv[0]);
				return -1;
			}
			sscanf(argv[argidx], "%x", (unsigned int *)
					&s_app.ftp.dev_type);
		} else if (strcmp(argv[argidx], "--media-list") == 0) {
			res = parse_media_list_param(argc, argv, &argidx);
			if (res < 0) {
				usage(argv[0]);
				return -1;
			}
		} else if (strcmp(argv[argidx], "--media-dl") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing uri\n");
				usage(argv[0]);
				return -1;
			}
			s_app.media.dl.uri = argv[argidx];

			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing local path\n");
				usage(argv[0]);
				return -1;
			}
			s_app.media.dl.local_path = argv[argidx];
			s_app.media.used = 1;
		} else if (strcmp(argv[argidx], "--media-delete") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing media id\n");
				usage(argv[0]);
				return -1;
			}
			s_app.media.del.name = argv[argidx];
			s_app.media.used = 1;
		} else if (strcmp(argv[argidx], "--update") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing firmware path\n");
				usage(argv[0]);
				return -1;
			}
			s_app.update.fw_filepath = argv[argidx];
			s_app.update.used = 1;
		} else if (strcmp(argv[argidx], "--update-dev-type") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing device type\n");
				usage(argv[0]);
				return -1;
			}
			sscanf(argv[argidx], "%x", (unsigned int *)
					&s_app.update.dev_type);
		} else if (strcmp(argv[argidx], "--update-cmp") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing firmware 1 path\n");
				usage(argv[0]);
				return -1;
			}
			s_app.update.fw_filepath = argv[argidx];
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing firmware 2 path\n");
				usage(argv[0]);
				return -1;
			}
			s_app.update.fw2_filepath = argv[argidx];
		} else if (strcmp(argv[argidx], "--crashml-dev-type") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing device type\n");
				usage(argv[0]);
				return -1;
			}
			sscanf(argv[argidx], "%x", (unsigned int *)
					&s_app.crashml.dev_type);
		} else if (strcmp(argv[argidx], "--crashml-dl") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing directory path\n");
				usage(argv[0]);
				return -1;
			}
			s_app.crashml.dir_path = argv[argidx];
			s_app.crashml.used = 1;
		} else if (strcmp(argv[argidx], "--crashml-type") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing crashml type\n");
				usage(argv[0]);
				return -1;
			}

			if (strcmp("dir", argv[argidx]) == 0) {
				s_app.crashml.types = ARSDK_CRASHML_TYPE_DIR;
			} else if (strcmp("tgz", argv[argidx]) == 0) {
				s_app.crashml.types = ARSDK_CRASHML_TYPE_TARGZ;
			} else {
				fprintf(stderr, "Bad crashml type filter.\n");
				usage(argv[0]);
				return -1;
			}
		} else if (strcmp(argv[argidx], "--flight-log-dev-type") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing device type\n");
				usage(argv[0]);
				return -1;
			}
			sscanf(argv[argidx], "%x", (unsigned int *)
					&s_app.flight_log.dev_type);
		} else if (strcmp(argv[argidx], "--flight-log-dl") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing directory path\n");
				usage(argv[0]);
				return -1;
			}
			s_app.flight_log.dir_path = argv[argidx];
			s_app.flight_log.used = 1;
		} else if (strcmp(argv[argidx], "--pud-dev-type") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing device type\n");
				usage(argv[0]);
				return -1;
			}
			sscanf(argv[argidx], "%x", (unsigned int *)
					&s_app.pud.dev_type);
		} else if (strcmp(argv[argidx], "--pud-dl") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing directory path\n");
				usage(argv[0]);
				return -1;
			}
			s_app.pud.dir_path = argv[argidx];
			s_app.pud.used = 1;
		} else if (strcmp(argv[argidx], "--ephemeris") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing file path\n");
				usage(argv[0]);
				return -1;
			}
			s_app.ephemeris.file_path = argv[argidx];
			s_app.ephemeris.used = 1;
		} else if (strcmp(argv[argidx], "--enable-blackbox") == 0) {
			s_app.blackbox.used = 1;
		} else if (strcmp(argv[argidx], "--tcp-send-dev-type") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing device type\n");
				usage(argv[0]);
				return -1;
			}
			sscanf(argv[argidx], "%x", (unsigned int *)
					&s_app.tcp_send.dev_type);
		} else if (strcmp(argv[argidx], "--tcp-send") == 0) {
			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing port\n");
				usage(argv[0]);
				return -1;
			}
			sscanf(argv[argidx], "%d", (unsigned int *)
					&s_app.tcp_send.port);

			argidx++;
			if ((argidx >= argc) || (argv[argidx][0] == '-')) {
				fprintf(stderr, "Missing data\n");
				usage(argv[0]);
				return -1;
			}
			s_app.tcp_send.data = argv[argidx];

			s_app.tcp_send.used = 1;
		}
	}

	/* tools without connection */
	run_simple_tools(&s_app);

	/* Create loop */
	s_app.loop = pomp_loop_new();
	s_app.timer = pomp_timer_new(s_app.loop, &timer_cb, &s_app);

	/* Create device manager */
	memset(&ctrl_device_cbs, 0, sizeof(ctrl_device_cbs));
	ctrl_device_cbs.userdata = &s_app;
	ctrl_device_cbs.added = &device_added;
	ctrl_device_cbs.removed = &device_removed;
	res = arsdk_ctrl_new(s_app.loop, &s_app.ctrl);
	if (res < 0)
		LOG_ERRNO("arsdk_ctrl_new", -res);

	arsdk_ctrl_set_device_cbs(s_app.ctrl, &ctrl_device_cbs);

	/* Create backend */
	if (s_app.backend_type == ARSDK_BACKEND_TYPE_MUX)
		mux_client_create(&s_app);

	backend_create(&s_app);

	/* Run loop */
	while (!s_app.stopped)
		pomp_loop_wait_and_process(s_app.loop, -1);

	/* Cleanup */
	if (s_app.tcp_send.used)
		tcp_send_stop(&s_app);

	if (s_app.backend_type == ARSDK_BACKEND_TYPE_MUX)
		mux_client_destroy(&s_app);
	else
		backend_destroy(&s_app);

	res = arsdk_ctrl_destroy(s_app.ctrl);
	if (res < 0)
		LOG_ERRNO("arsdk_ctrl_destroy", -res);

	res = pomp_timer_destroy(s_app.timer);
	if (res < 0)
		LOG_ERRNO("pomp_timer_destroy", -res);

	res = pomp_loop_destroy(s_app.loop);
	if (res < 0)
		LOG_ERRNO("pomp_loop_destroy", -res);

	return 0;
}
