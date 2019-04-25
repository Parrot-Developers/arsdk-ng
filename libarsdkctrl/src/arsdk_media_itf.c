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
#include "arsdk_media_itf_priv.h"
#include "arsdkctrl_default_log.h"

#define ROOT_FLD "/internal_000/"
#define MEDIA_FLD "media/"
#define THUMB_FLD "thumb/"

enum arsdk_media_req_type {
	ARSDK_MEDIA_REQ_LIST,
	ARSDK_MEDIA_REQ_DOWNLOAD,
	ARSDK_MEDIA_REQ_DELETE,
};

/** */
struct arsdk_media_itf {
	struct arsdk_ftp_itf            *ftp;
	struct list_node                reqs;
};

/** */
struct arsdk_media_res {
	uint32_t                        refcount;
	enum arsdk_media_res_type       type;
	enum arsdk_media_res_format     format;
	struct arsdk_ftp_file           *file;
	char                            *uri;
	struct list_node                node;
};

/** */
struct arsdk_media {
	uint32_t                        refcount;
	char                            *name;
	char                            *runid;
	enum arsdk_media_type           type;
	struct tm                       date;
	struct list_node                res;
	struct list_node                node;
};

/** */
struct arsdk_media_req_base {
	struct arsdk_media_itf          *itf;
	void                            *child;
	enum arsdk_media_req_type       type;
	uint8_t                         is_aborted;
	enum arsdk_device_type          dev_type;
	const char                      *dev_fld;
	struct list_node                node;
};

struct arsdk_media_list {
	uint32_t                        refcount;
	struct list_node                medias;
};

/** */
struct arsdk_media_req_list {
	struct arsdk_media_req_base         *base;
	struct arsdk_media_req_list_cbs     cbs;
	struct arsdk_ftp_req_list           *ftp_list_req;
	uint32_t                            types;
	struct arsdk_media_list             *result;
};

/** */
struct arsdk_media_req_download {
	struct arsdk_media_req_base         *base;
	struct arsdk_media_req_download_cbs cbs;
	char                                *uri;
	char                                *local_path;
	struct arsdk_ftp_req_get            *ftp_get_req;
};

/** */
struct arsdk_media_req_delete {
	struct arsdk_media_req_base         *base;
	struct arsdk_media_req_delete_cbs   cbs;
	struct arsdk_media                  *media;
	struct arsdk_ftp_req_delete         **reqs;
	size_t                              reqs_size;
	size_t                              reqs_nb;
	enum arsdk_media_req_status         status;
	int                                 error;
};

int arsdk_media_itf_new(struct arsdk_ftp_itf *ftp_itf,
		struct arsdk_media_itf **ret_itf)
{
	struct arsdk_media_itf *itf = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ftp_itf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_itf != NULL, -EINVAL);

	/* Allocate structure */
	itf = calloc(1, sizeof(*itf));
	if (itf == NULL)
		return -ENOMEM;

	itf->ftp = ftp_itf;
	list_init(&itf->reqs);

	*ret_itf = itf;
	return 0;
}

int arsdk_media_itf_destroy(struct arsdk_media_itf *itf)
{
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	arsdk_media_itf_stop(itf);

	free(itf);
	return 0;
}

static int parse_ext(const char *ext, enum arsdk_media_res_type *type,
		enum arsdk_media_res_format *fmt)
{
	static const struct {
		char                           *ext;
		enum arsdk_media_res_type      type;
		enum arsdk_media_res_format    fmt;
	} exts[] = {
		{"jpg", ARSDK_MEDIA_RES_TYPE_PHOTO, ARSDK_MEDIA_RES_FMT_JPG},
		{"mp4", ARSDK_MEDIA_RES_TYPE_VIDEO, ARSDK_MEDIA_RES_FMT_MP4},
		{"dng", ARSDK_MEDIA_RES_TYPE_PHOTO, ARSDK_MEDIA_RES_FMT_DNG},
	};
	static const size_t exts_count = sizeof(exts) / sizeof(exts[0]);

	size_t i = 0;

	for (i = 0; i < exts_count; i++) {
		if (strcmp(ext, exts[i].ext) == 0) {
			*type = exts[i].type;
			*fmt = exts[i].fmt;
			return 0;
		}
	}

	return -EINVAL;
}

static int arsdk_media_res_destroy(struct arsdk_media_res *res)
{
	ARSDK_RETURN_ERR_IF_FAILED(res != NULL, -EINVAL);

	arsdk_ftp_file_unref(res->file);

	free(res->uri);
	free(res);

	return 0;
}

static int arsdk_media_res_new(struct arsdk_media_res **ret_res)
{
	struct arsdk_media_res *resource = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_res != NULL, -EINVAL);

	resource = calloc(1, sizeof(*resource));
	if (resource == NULL)
		return -ENOMEM;

	resource->refcount = 1;

	*ret_res = resource;
	return 0;
}

static int arsdk_media_res_new_from_file(const char *path,
		struct arsdk_ftp_file *file,
		struct arsdk_media_res **ret_res)
{
	int res = 0;
	struct arsdk_media_res *resource = NULL;
	const char *name = NULL;
	char *ext = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(file != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_res != NULL, -EINVAL);

	name = arsdk_ftp_file_get_name(file);
	if (name == NULL)
		return -EINVAL;

	ext = strrchr(name, '.');
	if (ext == NULL)
		return -EINVAL;
	ext += 1;

	res = arsdk_media_res_new(&resource);
	if (res < 0)
		goto error;

	arsdk_ftp_file_ref(file);
	resource->file = file;

	res = parse_ext(ext, &resource->type, &resource->format);
	if (res < 0)
		goto error;

	res = asprintf(&resource->uri, "%s%s", path,
			arsdk_ftp_file_get_name(file));
	if (res < 0)
		goto error;

	*ret_res = resource;
	return 0;

error:
	arsdk_media_res_destroy(resource);
	return res;
}

static int parse_file_name(const char *name, char **product,
		char **date_str,
		char **runid,
		char **ext)
{
	char scan_product[11];
	char scan_date_str[31];
	char scan_runid[11];
	char scan_ext[11];
	char *cursor = NULL;
	int res = 0;

	ARSDK_RETURN_ERR_IF_FAILED(name != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(product != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(date_str != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ext != NULL, -EINVAL);

	/* reformat name
	 * [product name]_[date]_<runid>.[ext] to
	 * [product name] [date] <runid> [ext]
	 */

	cursor = strrchr(name, '.');
	if (cursor == NULL)
		return -EINVAL;
	*cursor = ' ';

	cursor = strrchr(name, '_');
	if (cursor == NULL)
		return -EINVAL;
	*cursor = ' ';

	cursor = strrchr(name, '_');
	if (cursor == NULL)
		return -EINVAL;
	*cursor = ' ';

	/* parsing */
	res = sscanf(name, "%10s %30s %10s %10s",
			scan_product, scan_date_str, scan_runid, scan_ext);

	if (res < 3)
		return -EINVAL;

	*product = xstrdup(scan_product);
	*date_str = xstrdup(scan_date_str);

	if (res == 3) {
		/* no runid*/
		*runid = NULL;
		*ext = xstrdup(scan_runid);
	} else {
		*runid = xstrdup(scan_runid);
		*ext = xstrdup(scan_ext);
	}

	return 0;
}

static int ext_to_type(const char *ext, enum arsdk_media_type *type)
{
	static const struct {
		char                       *ext;
		enum arsdk_media_type      type;
	} exts[] = {
		{"jpg", ARSDK_MEDIA_TYPE_PHOTO},
		{"mp4", ARSDK_MEDIA_TYPE_VIDEO},
		{"dng", ARSDK_MEDIA_TYPE_PHOTO},
	};
	static const size_t exts_count = sizeof(exts) / sizeof(exts[0]);

	size_t i = 0;

	for (i = 0; i < exts_count; i++) {
		if (strcmp(ext, exts[i].ext) == 0) {
			*type = exts[i].type;
			return 0;
		}
	}

	return -EINVAL;
}

static uint8_t filter_type(uint32_t types, enum arsdk_media_res_type res_type)
{
	switch (res_type) {
	case ARSDK_MEDIA_RES_TYPE_PHOTO:
		return types & ARSDK_MEDIA_TYPE_PHOTO;
	case ARSDK_MEDIA_RES_TYPE_VIDEO:
		return types & ARSDK_MEDIA_TYPE_VIDEO;
	default:
		return 0;
	}
}

static int arsdk_media_res_new_thumb(struct arsdk_media_req_list *req_list,
		const char *product,
		const char *date_str,
		const char *ext,
		enum arsdk_media_type type,
		const char *runid,
		struct arsdk_media_res **ret_res)
{
	struct arsdk_ftp_file *thumb_file = NULL;
	const char *runid_str = "";
	char *name = NULL;
	char *thumb_path = NULL;
	int res = 0;

	res = arsdk_ftp_file_new(&thumb_file);
	if (res < 0)
		return res;

	switch (type) {
	case ARSDK_MEDIA_TYPE_PHOTO:
		if (runid != NULL)
			runid_str = runid;

		res = asprintf(&name, "%s_%s_%s.jpg",
				product,
				date_str,
				runid_str);
		if (res < 0)
			goto error;
		break;
	case ARSDK_MEDIA_TYPE_VIDEO:
		if (runid != NULL)
			runid_str = runid;

		res = asprintf(&name, "%s_%s_%s.%s.jpg",
				product,
				date_str,
				runid_str,
				ext);
		if (res < 0)
			goto error;
		break;
	default:
		res = -EINVAL;
		goto error;
	}

	res = arsdk_ftp_file_set_name(thumb_file, name);
	if (res < 0)
		goto error;

	res = asprintf(&thumb_path, "%s%s/%s", ROOT_FLD,
			req_list->base->dev_fld, THUMB_FLD);
	if (res < 0)
		goto error;

	res = arsdk_media_res_new_from_file(thumb_path, thumb_file, ret_res);
	if (res < 0)
		goto error;

	(*ret_res)->type = ARSDK_MEDIA_RES_TYPE_THUMBNAIL;
	free(name);
	free(thumb_path);
	arsdk_ftp_file_unref(thumb_file);

	return 0;

error:
	free(name);
	free(thumb_path);
	arsdk_ftp_file_unref(thumb_file);
	return res;
}

static int file_to_media_name(struct arsdk_ftp_file *file, char **ret_name)
{
	const char *f_name = NULL;
	char *name_end = NULL;
	char *name = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(file != NULL, -EINVAL);

	f_name = arsdk_ftp_file_get_name(file);
	name = xstrdup(f_name);

	name_end = strrchr(name, '.');
	if (name_end == NULL) {
		free(name);
		return -EINVAL;
	}
	*name_end = '\0';

	*ret_name = name;
	return 0;
}

static int apply_tzone(struct tm *date, const char *tzone_str)
{
	struct tm tzone;
	const char *fmt_res = NULL;
	time_t date_s;
	long tzone_s;

	ARSDK_RETURN_ERR_IF_FAILED(date != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(tzone_str != NULL, -EINVAL);

	if (tzone_str[0] != '+' && tzone_str[0] != '-')
		return 0;

	/* parse time zone*/
	fmt_res = strptime(tzone_str + 1, "%H%M", &tzone);

	if ((fmt_res == NULL) ||
	    (*fmt_res != '\0')) {
		return -EINVAL;
	}

	tzone_s = (tzone.tm_hour * 3600) + (tzone.tm_min * 60);
	date_s = mktime(date);
	if (tzone_str[0] == '+')
		date_s += tzone_s;
	else if (tzone_str[0] == '-')
		date_s -= tzone_s;
	else
		return -EINVAL;

	/* update date */
	gmtime_r(&date_s, date);

	return 0;
}

static int file_to_media(struct arsdk_media_req_list *req_list,
		const char *path,
		struct arsdk_ftp_file *file,
		struct arsdk_media *media)
{
	const char *name = NULL;
	char *name_cp = NULL;
	char *product = NULL;
	char *date_str = NULL;
	char *ext = NULL;
	int res = 0;
	char *fmt_res = NULL;
	struct arsdk_media_res *thumb;

	ARSDK_RETURN_ERR_IF_FAILED(file != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(media != NULL, -EINVAL);

	res = file_to_media_name(file, &media->name);
	if (res < 0)
		goto cleanup;

	name = arsdk_ftp_file_get_name(file);
	name_cp = xstrdup(name);
	res = parse_file_name(name_cp, &product, &date_str,
			&media->runid, &ext);
	if (res < 0)
		goto cleanup;

	res = ext_to_type(ext, &media->type);
	if (res < 0)
		goto cleanup;

	memset(&media->date, 0, sizeof(media->date));
	fmt_res = strptime(date_str, "%Y-%m-%dT%H%M%S", &media->date);
	if (fmt_res == NULL) {
		res = -EINVAL;
		goto cleanup;
	}

	/* Apply the timezone to the date. */
	res = apply_tzone(&media->date, fmt_res);
	if (res < 0)
		goto cleanup;

	/* Add thumbnail resource */
	res = arsdk_media_res_new_thumb(req_list,
			product,
			date_str,
			ext,
			media->type,
			media->runid,
			&thumb);
	if (res < 0)
		goto cleanup;

	list_add_after(&media->res, &thumb->node);

cleanup:
	free(name_cp);
	free(product);
	free(date_str);
	free(ext);
	return res;
}

static int arsdk_media_destroy(struct arsdk_media *media)
{
	struct arsdk_media_res *res;
	struct arsdk_media_res *res_tmp;

	ARSDK_RETURN_ERR_IF_FAILED(media != NULL, -EINVAL);

	list_walk_entry_forward_safe(&media->res, res, res_tmp, node) {
		list_del(&res->node);
		arsdk_media_res_destroy(res);
	}

	free(media->name);
	free(media->runid);
	free(media);

	return 0;
}

static int arsdk_media_new(struct arsdk_media **ret_media)
{
	struct arsdk_media *media = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_media != NULL, -EINVAL);

	media = calloc(1, sizeof(*media));
	if (media == NULL)
		return -ENOMEM;

	media->refcount = 1;
	list_init(&media->res);

	*ret_media = media;
	return 0;
}

static int arsdk_media_list_destroy(struct arsdk_media_list *list)
{
	struct arsdk_media *media;
	struct arsdk_media *media_tmp;

	ARSDK_RETURN_ERR_IF_FAILED(list != NULL, -EINVAL);

	list_walk_entry_forward_safe(&list->medias, media, media_tmp, node) {
		list_del(&media->node);
		arsdk_media_unref(media);
	}

	free(list);

	return 0;
}

static int arsdk_media_list_new(struct arsdk_media_list **ret_list)
{
	struct arsdk_media_list *list = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_list != NULL, -EINVAL);

	list = calloc(1, sizeof(*list));
	if (list == NULL)
		return -ENOMEM;

	list->refcount = 1;
	list_init(&list->medias);

	*ret_list = list;
	return 0;
}

static int arsdk_media_new_from_file(struct arsdk_media_req_list *req_list,
		const char *path,
		struct arsdk_ftp_file *file,
		struct arsdk_media **ret_media)
{
	int res = 0;
	struct arsdk_media *media = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(file != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_media != NULL, -EINVAL);

	res = arsdk_media_new(&media);
	if (res < 0)
		goto error;

	res = file_to_media(req_list, path, file, media);
	if (res < 0)
		goto error;

	*ret_media = media;
	return 0;

error:
	arsdk_media_unref(media);
	return res;
}

/*
 * See documentation in public header.
 */
void arsdk_media_ref(struct arsdk_media *media)
{
	ARSDK_RETURN_IF_FAILED(media != NULL, -EINVAL);

	media->refcount++;
}

/*
 * See documentation in public header.
 */
void arsdk_media_unref(struct arsdk_media *media)
{
	ARSDK_RETURN_IF_FAILED(media != NULL, -EINVAL);

	media->refcount--;

	/* Free resource when ref count reaches 0 */
	if (media->refcount == 0)
		(void)arsdk_media_destroy(media);
}

/**
 */
static void req_destroy(struct arsdk_media_req_base *req)
{
	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);

	free(req);
}

/**
 */
static int req_new(struct arsdk_media_itf *itf,
		void *child,
		enum arsdk_media_req_type type,
		enum arsdk_device_type dev_type,
		struct arsdk_media_req_base **ret_req)
{
	int res = 0;
	struct arsdk_media_req_base *req = NULL;

	*ret_req = NULL;

	/* Allocate structure */
	req = calloc(1, sizeof(*req));
	if (req == NULL)
		return -ENOMEM;

	/* Initialize structure */
	req->itf = itf;
	req->child = child;
	req->type = type;
	req->dev_type = dev_type;
	req->dev_fld = arsdk_device_type_to_fld(dev_type);
	if (req->dev_fld == NULL) {
		res = -EINVAL;
		goto error;
	}

	*ret_req = req;
	return 0;

error:
	req_destroy(req);
	return res;
}

/**
 */
static void arsdk_media_req_list_destroy(
	struct arsdk_media_req_list *req_list)
{
	ARSDK_RETURN_IF_FAILED(req_list != NULL, -EINVAL);

	if (req_list->ftp_list_req != NULL)
		ARSDK_LOGW("request %p still pending", req_list);

	req_destroy(req_list->base);
	arsdk_media_list_unref(req_list->result);

	free(req_list);
}

static enum arsdk_media_req_status ftp_to_media_status(
		enum arsdk_ftp_req_status ftp_status)
{
	switch (ftp_status) {
	case ARSDK_FTP_REQ_STATUS_OK:
		return ARSDK_MEDIA_REQ_STATUS_OK;
	case ARSDK_FTP_REQ_STATUS_CANCELED:
		return ARSDK_MEDIA_REQ_STATUS_CANCELED;
	case ARSDK_FTP_REQ_STATUS_FAILED:
		return ARSDK_MEDIA_REQ_STATUS_FAILED;
	case ARSDK_FTP_REQ_STATUS_ABORTED:
		return ARSDK_MEDIA_REQ_STATUS_ABORTED;
	default:
		ARSDK_LOGE("ftp status not known. %d", ftp_status);
		return ARSDK_MEDIA_REQ_STATUS_FAILED;
	}
}

static void pfld_list_complete_cb(struct arsdk_ftp_itf *itf,
			struct arsdk_ftp_req_list *req,
			enum arsdk_ftp_req_status status,
			int error,
			void *userdata)
{
	struct arsdk_media_req_list *req_list = userdata;
	int res = error;
	struct arsdk_ftp_file_list *file_list = NULL;
	struct arsdk_ftp_file *next = NULL;
	struct arsdk_ftp_file *curr = NULL;
	struct arsdk_media *media_tmp = NULL;
	struct arsdk_media *media = NULL;
	struct arsdk_media_res *resource = NULL;
	const char *path = NULL;
	char *media_name = NULL;
	struct arsdk_media_list *response = NULL;
	enum arsdk_media_req_status media_status = ARSDK_MEDIA_REQ_STATUS_OK;

	ARSDK_RETURN_IF_FAILED(req_list != NULL, -EINVAL);

	if (req_list->base->is_aborted) {
		media_status = ARSDK_MEDIA_REQ_STATUS_ABORTED;
		goto end;
	}

	if (status != ARSDK_FTP_REQ_STATUS_OK) {
		media_status = ftp_to_media_status(status);
		res = error;
		goto end;
	}

	if (req == NULL) {
		media_status = ARSDK_MEDIA_REQ_STATUS_FAILED;
		res = -EINVAL;
		goto end;
	}

	res = arsdk_media_list_new(&response);
	if (res < 0) {
		media_status = ARSDK_MEDIA_REQ_STATUS_FAILED;
		res = -ENOMEM;
		goto end;
	}
	req_list->result = response;

	path = arsdk_ftp_req_list_get_path(req);

	file_list = arsdk_ftp_req_list_get_result(req);
	next = arsdk_ftp_file_list_next_file(file_list, curr);
	while (next != NULL) {
		curr = next;
		next = arsdk_ftp_file_list_next_file(file_list, curr);

		/* create the resource */
		res = arsdk_media_res_new_from_file(path, curr, &resource);
		if (res < 0)
			continue;

		/* media filter */
		res = filter_type(req_list->types, resource->type);
		if (!res) {
			arsdk_media_res_destroy(resource);
			continue;
		}

		res = file_to_media_name(curr, &media_name);
		if (res < 0) {
			arsdk_media_res_destroy(resource);
			continue;
		}

		/* search the media */
		media = NULL;
		list_walk_entry_forward(&response->medias, media_tmp, node) {
			if (strcmp(media_tmp->name, media_name) == 0) {
				media = media_tmp;
				break;
			}
		}

		if (media == NULL) {
			/* create the media */
			res = arsdk_media_new_from_file(req_list, path, curr,
					&media);
			if (res < 0) {
				arsdk_media_res_destroy(resource);
				free(media_name);
				continue;
			}

			list_add_after(&response->medias, &media->node);
		}

		list_add_after(&media->res, &resource->node);
		free(media_name);
	}

end:
	req_list->ftp_list_req = NULL;

	(*req_list->cbs.complete)(req_list->base->itf,
			req_list,
			media_status,
			res,
			req_list->cbs.userdata);

	/* cleanup */
	list_del(&req_list->base->node);
	arsdk_media_req_list_destroy(req_list);
}

int arsdk_media_itf_create_req_list(
		struct arsdk_media_itf *itf,
		const struct arsdk_media_req_list_cbs *cbs,
		uint32_t types,
		enum arsdk_device_type dev_type,
		struct arsdk_media_req_list **ret_req)
{
	int res = 0;
	struct arsdk_media_req_list *req_list = NULL;
	struct arsdk_ftp_req_list_cbs ftp_cbs;
	char dev_fld_path[500] = "";

	ARSDK_RETURN_ERR_IF_FAILED(ret_req != NULL, -EINVAL);
	*ret_req = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->complete != NULL, -EINVAL);

	/* Allocate structure */
	req_list = calloc(1, sizeof(*req_list));
	if (req_list == NULL)
		return -ENOMEM;

	res = req_new(itf, req_list, ARSDK_MEDIA_REQ_LIST, dev_type,
			&req_list->base);
	if (res < 0)
		goto error;

	req_list->cbs = *cbs;
	req_list->types = types;

	ftp_cbs.userdata = req_list;
	ftp_cbs.complete = &pfld_list_complete_cb;

	snprintf(dev_fld_path, sizeof(dev_fld_path), "%s%s/%s", ROOT_FLD,
			req_list->base->dev_fld, MEDIA_FLD);

	res =  arsdk_ftp_itf_create_req_list(itf->ftp, &ftp_cbs, dev_type,
			ARSDK_FTP_SRV_TYPE_MEDIA, dev_fld_path,
			&req_list->ftp_list_req);
	if (res < 0)
		goto error;

	list_add_after(&itf->reqs, &req_list->base->node);
	*ret_req = req_list;
	return 0;

error:
	arsdk_media_req_list_destroy(req_list);
	return res;
}

int arsdk_media_req_list_cancel(struct arsdk_media_req_list *req)
{
	struct arsdk_media_itf *itf = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->base != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->base->itf != NULL, -EINVAL);

	itf = req->base->itf;
	return arsdk_ftp_req_list_cancel(req->ftp_list_req);
}

static int arsdk_media_req_list_abort(struct arsdk_media_req_list *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->base != NULL, -EINVAL);

	req->base->is_aborted = 1;
	return arsdk_media_req_list_cancel(req);
}

enum arsdk_device_type arsdk_media_req_list_get_dev_type(
		const struct arsdk_media_req_list *req)
{
	if ((req == NULL) ||
	    (req->base == NULL))
		return ARSDK_DEVICE_TYPE_UNKNOWN;

	return req->base->dev_type;
}

struct arsdk_media_list *arsdk_media_req_list_get_result(
		struct arsdk_media_req_list *req)
{
	if (req == NULL)
		return NULL;

	return req->result;
}

struct arsdk_media *arsdk_media_list_next_media(
		struct arsdk_media_list *list,
		struct arsdk_media *prev)
{
	struct list_node *node;
	struct arsdk_media *next;

	if (!list)
		return NULL;

	node = list_next(&list->medias, prev ? &prev->node : &list->medias);
	if (!node)
		return NULL;

	next = list_entry(node, struct arsdk_media, node);
	return next;
}

size_t arsdk_media_list_get_count(
		struct arsdk_media_list *list)
{
	if (!list)
		return 0;

	return list_length(&list->medias);
}

void arsdk_media_list_ref(struct arsdk_media_list *list)
{
	ARSDK_RETURN_IF_FAILED(list != NULL, -EINVAL);

	list->refcount++;
}


void arsdk_media_list_unref(struct arsdk_media_list *list)
{
	ARSDK_RETURN_IF_FAILED(list != NULL, -EINVAL);

	list->refcount--;

	/* Free resource when ref count reaches 0 */
	if (list->refcount == 0)
		(void)arsdk_media_list_destroy(list);
}

/**
 */
static void arsdk_media_req_download_destroy(
	struct arsdk_media_req_download *req)
{
	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);

	if (req->ftp_get_req != NULL)
		ARSDK_LOGW("request %p still pending", req);

	req_destroy(req->base);
	free(req->uri);
	free(req->local_path);
	free(req);
}

static void ftpget_progress_cb(struct arsdk_ftp_itf *itf,
		struct arsdk_ftp_req_get *req,
		float percent,
		void *userdata)
{
	struct arsdk_media_req_download *req_dl = userdata;

	ARSDK_RETURN_IF_FAILED(req_dl != NULL, -EINVAL);

	(*req_dl->cbs.progress)(req_dl->base->itf,
			req_dl,
			percent,
			req_dl->cbs.userdata);

}

static void ftpget_complete_cb(struct arsdk_ftp_itf *itf,
		struct arsdk_ftp_req_get *req,
		enum arsdk_ftp_req_status status,
		int error,
		void *userdata)
{
	struct arsdk_media_req_download *req_dl = userdata;
	enum arsdk_media_req_status media_status = ftp_to_media_status(status);

	ARSDK_RETURN_IF_FAILED(req_dl != NULL, -EINVAL);

	if (req_dl->base->is_aborted)
		media_status = ARSDK_MEDIA_REQ_STATUS_ABORTED;

	(*req_dl->cbs.complete)(req_dl->base->itf,
			req_dl,
			media_status,
			error,
			req_dl->cbs.userdata);

	/* cleanup */
	req_dl->ftp_get_req = NULL;
	list_del(&req_dl->base->node);
	arsdk_media_req_download_destroy(req_dl);
}

int arsdk_media_itf_create_req_download(
		struct arsdk_media_itf *itf,
		const struct arsdk_media_req_download_cbs *cbs,
		const char *uri,
		const char *local_path,
		enum arsdk_device_type dev_type,
		uint8_t is_resume,
		struct arsdk_media_req_download **ret_req)
{
	int res = 0;
	struct arsdk_media_req_download *req_dl = NULL;
	struct arsdk_ftp_req_get_cbs ftp_cbs;

	ARSDK_RETURN_ERR_IF_FAILED(ret_req != NULL, -EINVAL);
	*ret_req = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->complete != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->progress != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(uri != NULL, -EINVAL);

	/* Allocate structure */
	req_dl = calloc(1, sizeof(*req_dl));
	if (req_dl == NULL)
		return -ENOMEM;

	res = req_new(itf, req_dl, ARSDK_MEDIA_REQ_DOWNLOAD, dev_type,
			&req_dl->base);
	if (res < 0)
		goto error;

	req_dl->cbs = *cbs;
	req_dl->uri = xstrdup(uri);
	req_dl->local_path = xstrdup(local_path);

	ftp_cbs.userdata = req_dl;
	ftp_cbs.complete = &ftpget_complete_cb;
	ftp_cbs.progress = &ftpget_progress_cb;

	res =  arsdk_ftp_itf_create_req_get(itf->ftp, &ftp_cbs, dev_type,
			ARSDK_FTP_SRV_TYPE_MEDIA, uri, local_path, is_resume,
			&req_dl->ftp_get_req);
	if (res < 0)
		goto error;

	list_add_after(&itf->reqs, &req_dl->base->node);
	*ret_req = req_dl;
	return 0;

error:
	arsdk_media_req_download_destroy(req_dl);
	return res;
}

int arsdk_media_req_download_cancel(struct arsdk_media_req_download *req)
{
	struct arsdk_media_itf *itf = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->base != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->base->itf != NULL, -EINVAL);

	itf = req->base->itf;
	return arsdk_ftp_req_get_cancel(req->ftp_get_req);
}

static int arsdk_media_req_download_abort(
		struct arsdk_media_req_download *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->base != NULL, -EINVAL);

	req->base->is_aborted = 1;
	return arsdk_media_req_download_cancel(req);
}

const char *arsdk_media_req_download_get_uri(
		const struct arsdk_media_req_download *req)
{
	if (!req)
		return NULL;

	return req->uri;
}

const char *arsdk_media_req_download_get_local_path(
		const struct arsdk_media_req_download *req)
{
	if (!req)
		return NULL;

	return req->local_path;
}

struct pomp_buffer *arsdk_media_req_download_get_buffer(
		const struct arsdk_media_req_download *req)
{
	if ((req == NULL) ||
	    (req->ftp_get_req == NULL))
		return NULL;

	return arsdk_ftp_req_get_get_buffer(req->ftp_get_req);
}

enum arsdk_device_type arsdk_media_req_download_get_dev_type(
		const struct arsdk_media_req_download *req)
{
	if ((req == NULL) ||
	    (req->base == NULL))
		return ARSDK_DEVICE_TYPE_UNKNOWN;

	return req->base->dev_type;
}

struct arsdk_media_res *arsdk_media_next_res(
		struct arsdk_media *media,
		struct arsdk_media_res *prev)
{
	struct list_node *node;
	struct arsdk_media_res *next;

	if (!media)
		return NULL;

	node = list_next(&media->res, prev ? &prev->node : &media->res);
	if (!node)
		return NULL;

	next = list_entry(node, struct arsdk_media_res, node);
	return next;
	return NULL;
}

size_t arsdk_media_get_res_count(const struct arsdk_media *media)
{
	if (!media)
		return 0;

	return list_length(&media->res);
}

/**
 */
static void arsdk_media_req_delete_destroy(
	struct arsdk_media_req_delete *req)
{
	ARSDK_RETURN_IF_FAILED(req != NULL, -EINVAL);

	if (req->reqs_nb != 0)
		ARSDK_LOGW("request %p still pending", req);

	free(req->reqs);
	arsdk_media_unref(req->media);
	req_destroy(req->base);
	free(req);
}

static void ftpdel_complete_cb(struct arsdk_ftp_itf *itf,
		struct arsdk_ftp_req_delete *req,
		enum arsdk_ftp_req_status status,
		int error,
		void *userdata)
{
	struct arsdk_media_req_delete *req_del = userdata;
	size_t i = 0;

	ARSDK_RETURN_IF_FAILED(req_del != NULL, -EINVAL);

	if (req_del->base->is_aborted) {
		req_del->status = ARSDK_MEDIA_REQ_STATUS_ABORTED;
		goto end;
	}

	if (req_del->status != ARSDK_MEDIA_REQ_STATUS_OK)
		goto end;

	switch (status) {
	case ARSDK_FTP_REQ_STATUS_FAILED:
		break;
	case ARSDK_FTP_REQ_STATUS_CANCELED:
	case ARSDK_FTP_REQ_STATUS_ABORTED:
		req_del->status = ftp_to_media_status(status);
		break;
	case ARSDK_FTP_REQ_STATUS_OK:
		break;
	default:
		break;
	}

end:
	/* Remove request form the array */
	for (i = 0; i < req_del->reqs_size; i++) {
		if (req_del->reqs[i] == req) {
			req_del->reqs[i] = NULL;
			req_del->reqs_nb--;
			break;
		}
	}

	if (req_del->reqs_nb != 0)
		return;

	(*req_del->cbs.complete)(req_del->base->itf,
			req_del,
			req_del->status,
			req_del->error,
			req_del->cbs.userdata);

	/* cleanup */
	list_del(&req_del->base->node);
	arsdk_media_req_delete_destroy(req_del);
}

int arsdk_media_itf_create_req_delete(
	struct arsdk_media_itf *itf,
	const struct arsdk_media_req_delete_cbs *cbs,
	struct arsdk_media *media,
	enum arsdk_device_type dev_type,
	struct arsdk_media_req_delete **ret_req)
{
	int res = 0;
	struct arsdk_media_req_delete *req_del = NULL;
	struct arsdk_ftp_req_delete_cbs ftp_cbs;
	struct arsdk_media_res *resource = NULL;
	const char *uri = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_req != NULL, -EINVAL);
	*ret_req = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->complete != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(media != NULL, -EINVAL);

	/* Allocate structure */
	req_del = calloc(1, sizeof(*req_del));
	if (req_del == NULL)
		return -ENOMEM;

	res = req_new(itf, req_del, ARSDK_MEDIA_REQ_DELETE, dev_type,
			&req_del->base);
	if (res < 0)
		goto error;

	req_del->reqs_size = arsdk_media_get_res_count(media);
	req_del->reqs = calloc(req_del->reqs_size, sizeof(*req_del->reqs));
	if (req_del->reqs == NULL) {
		res = -ENOMEM;
		goto error;
	}

	arsdk_media_ref(media);
	req_del->media = media;
	req_del->status = ARSDK_MEDIA_REQ_STATUS_OK;
	req_del->cbs = *cbs;

	ftp_cbs.userdata = req_del;
	ftp_cbs.complete = &ftpdel_complete_cb;
	resource = arsdk_media_next_res(media, resource);
	while (resource != NULL) {
		uri = arsdk_media_res_get_uri(resource);

		res =  arsdk_ftp_itf_create_req_delete(itf->ftp, &ftp_cbs,
				dev_type,
				ARSDK_FTP_SRV_TYPE_MEDIA, uri,
				&req_del->reqs[req_del->reqs_nb]);
		if (res < 0)
			goto error;

		req_del->reqs_nb++;
		resource = arsdk_media_next_res(media, resource);
	}

	list_add_after(&itf->reqs, &req_del->base->node);
	*ret_req = req_del;
	return 0;

error:
	arsdk_media_req_delete_destroy(req_del);
	return res;
}

static int arsdk_media_req_delete_cancel(struct arsdk_media_req_delete *req)
{
	size_t i = 0;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	for (i = 0; i < req->reqs_size; i++) {
		if (req->reqs[i] == NULL)
			continue;

		arsdk_ftp_req_delete_cancel(req->reqs[i]);
	}

	return 0;
}

static int arsdk_media_req_delete_abort(struct arsdk_media_req_delete *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req->base != NULL, -EINVAL);

	req->base->is_aborted = 1;
	return arsdk_media_req_delete_cancel(req);
}

const struct arsdk_media *arsdk_media_req_delete_get_media(
		const struct arsdk_media_req_delete *req)
{
	if (!req)
		return NULL;

	return req->media;
}

enum arsdk_device_type arsdk_media_req_delete_get_dev_type(
		const struct arsdk_media_req_delete *req)
{
	if ((req == NULL) ||
	    (req->base == NULL))
		return ARSDK_DEVICE_TYPE_UNKNOWN;

	return req->base->dev_type;
}

static int arsdk_media_req_base_cancel(struct arsdk_media_req_base *base)
{
	ARSDK_RETURN_ERR_IF_FAILED(base != NULL, -EINVAL);

	switch (base->type) {
	case ARSDK_MEDIA_REQ_LIST:
		return arsdk_media_req_list_cancel(base->child);
	case ARSDK_MEDIA_REQ_DOWNLOAD:
		return arsdk_media_req_download_cancel(base->child);
	case ARSDK_MEDIA_REQ_DELETE:
		return arsdk_media_req_delete_cancel(base->child);
	default:
		return -EINVAL;
	}
}

int arsdk_media_itf_cancel_all(struct arsdk_media_itf *itf)
{
	struct arsdk_media_req_base *base = NULL;
	struct arsdk_media_req_base *base_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	list_walk_entry_forward_safe(&itf->reqs, base, base_tmp, node) {
		arsdk_media_req_base_cancel(base);
	}

	return 0;
}

static int arsdk_media_req_base_abort(struct arsdk_media_req_base *base)
{
	ARSDK_RETURN_ERR_IF_FAILED(base != NULL, -EINVAL);

	switch (base->type) {
	case ARSDK_MEDIA_REQ_LIST:
		return arsdk_media_req_list_abort(base->child);
	case ARSDK_MEDIA_REQ_DOWNLOAD:
		return arsdk_media_req_download_abort(base->child);
	case ARSDK_MEDIA_REQ_DELETE:
		return arsdk_media_req_delete_abort(base->child);
	default:
		return -EINVAL;
	}
}

static int arsdk_media_itf_abort_all(struct arsdk_media_itf *itf)
{
	struct arsdk_media_req_base *base = NULL;
	struct arsdk_media_req_base *base_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	list_walk_entry_forward_safe(&itf->reqs, base, base_tmp, node) {
		arsdk_media_req_base_abort(base);
	}

	return 0;
}

int arsdk_media_itf_stop(struct arsdk_media_itf *itf)
{
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	arsdk_media_itf_abort_all(itf);

	return 0;
}

const char *arsdk_media_get_name(const struct arsdk_media *media)
{
	return (media != NULL) ? media->name : NULL;
}

const char *arsdk_media_get_runid(const struct arsdk_media *media)
{
	return (media != NULL) ? media->runid : NULL;
}

const struct tm *arsdk_media_get_date(const struct arsdk_media *media)
{
	return (media != NULL) ? &media->date : NULL;
}

enum arsdk_media_type arsdk_media_get_type(const struct arsdk_media *media)
{
	return (media != NULL) ? media->type : ARSDK_MEDIA_TYPE_UNKNOWN;
}

enum arsdk_media_res_type arsdk_media_res_get_type(
		const struct arsdk_media_res *resource)
{
	return (resource != NULL) ? resource->type :
			ARSDK_MEDIA_RES_TYPE_UNKNOWN;
}

enum arsdk_media_res_format arsdk_media_res_get_fmt(
		const struct arsdk_media_res *resource)
{
	return (resource != NULL) ? resource->format :
			ARSDK_MEDIA_RES_FMT_UNKNOWN;
}

const char *arsdk_media_res_get_uri(const struct arsdk_media_res *resource)
{
	return (resource != NULL) ? resource->uri : NULL;
}

size_t arsdk_media_res_get_size(const struct arsdk_media_res *resource)
{
	if (resource == NULL)
		return 0;

	return arsdk_ftp_file_get_size(resource->file);
}

const char *arsdk_media_res_get_name(const struct arsdk_media_res *resource)
{
	if (resource == NULL)
		return NULL;

	return arsdk_ftp_file_get_name(resource->file);
}
