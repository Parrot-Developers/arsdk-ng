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
#include "updater/arsdk_updater_transport.h"
#include <ctype.h>

#include "updater/arsdk_updater_transport_ftp.h"
#include "updater/arsdk_updater_transport_mux.h"

#include "arsdk_updater_itf_priv.h"
#ifdef BUILD_LIBPUF
#  include <libpuf.h>
#  include <fcntl.h>
#endif /* !BUILD_LIBPUF */

/** */
struct arsdk_updater_itf {
	struct arsdk_device_info                *dev_info;
	struct arsdk_updater_transport_ftp      *ftp_tsprt;
	struct arsdk_updater_transport_mux      *mux_tsprt;
};

/** */
struct arsdk_updater_req_upload {
	void                                    *child;
	struct arsdk_updater_req_upload_cbs     cbs;
	struct arsdk_updater_transport          *tsprt;
	enum arsdk_device_type                  dev_type;
};

int arsdk_updater_itf_new(struct arsdk_device_info *dev_info,
		struct arsdk_ftp_itf *ftp_itf,
		struct mux_ctx *mux,
		struct arsdk_updater_itf **ret_itf)
{
	int res = 0;
	struct arsdk_updater_itf *itf = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_itf != NULL, -EINVAL);
	*ret_itf = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(dev_info != NULL, -EINVAL);

	/* Allocate structure */
	itf = calloc(1, sizeof(*itf));
	if (itf == NULL)
		return -ENOMEM;

	/* Initialize structure */
	itf->dev_info = dev_info;

	if (ftp_itf != NULL) {
		res = arsdk_updater_transport_ftp_new(itf, ftp_itf,
				&itf->ftp_tsprt);
		if (res < 0)
			goto error;
	}
	if (mux != NULL) {
		res = arsdk_updater_transport_mux_new(itf, mux,
				&itf->mux_tsprt);
		if (res < 0)
			goto error;
	}

	*ret_itf = itf;
	return 0;
error:
	arsdk_updater_itf_destroy(itf);
	return res;
}

int arsdk_updater_itf_stop(struct arsdk_updater_itf *itf)
{
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	if (itf->ftp_tsprt != NULL)
		arsdk_updater_transport_ftp_stop(itf->ftp_tsprt);

	if (itf->mux_tsprt != NULL)
		arsdk_updater_transport_mux_stop(itf->mux_tsprt);

	return 0;
}

int arsdk_updater_itf_cancel_all(struct arsdk_updater_itf *itf)
{
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	if (itf->ftp_tsprt != NULL)
		arsdk_updater_transport_ftp_cancel_all(itf->ftp_tsprt);

	if (itf->mux_tsprt != NULL)
		arsdk_updater_transport_mux_cancel_all(itf->mux_tsprt);

	return 0;
}

int arsdk_updater_itf_destroy(struct arsdk_updater_itf *itf)
{
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	arsdk_updater_itf_stop(itf);

	if (itf->ftp_tsprt != NULL)
		arsdk_updater_transport_ftp_destroy(itf->ftp_tsprt);

	if (itf->mux_tsprt != NULL)
		arsdk_updater_transport_mux_destroy(itf->mux_tsprt);

	free(itf);
	return 0;
}

static struct arsdk_updater_transport *get_tsprt(struct arsdk_updater_itf *itf,
		enum arsdk_device_type dev_type)
{
	if (itf == NULL)
		return NULL;

	switch (itf->dev_info->backend_type) {
	case ARSDK_BACKEND_TYPE_NET:
		return arsdk_updater_transport_ftp_get_parent(itf->ftp_tsprt);
	case ARSDK_BACKEND_TYPE_MUX:
		if (dev_type == ARSDK_DEVICE_TYPE_SKYCTRL_2 ||
		    dev_type == ARSDK_DEVICE_TYPE_SKYCTRL_NG ||
		    dev_type == ARSDK_DEVICE_TYPE_SKYCTRL_3 ||
		    dev_type == ARSDK_DEVICE_TYPE_SKYCTRL_UA ||
		    dev_type == ARSDK_DEVICE_TYPE_SKYCTRL_4)
			return arsdk_updater_transport_mux_get_parent(
					itf->mux_tsprt);
		else
			return arsdk_updater_transport_ftp_get_parent(
					itf->ftp_tsprt);
	case ARSDK_BACKEND_TYPE_UNKNOWN:
	default:
		return NULL;
	}
}

int arsdk_updater_itf_create_req_upload(struct arsdk_updater_itf *itf,
		const char *fw_filepath,
		enum arsdk_device_type dev_type,
		const struct arsdk_updater_req_upload_cbs *cbs,
		struct arsdk_updater_req_upload **ret_req)
{
	struct arsdk_updater_transport *tsprt = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	tsprt = get_tsprt(itf, dev_type);

	return arsdk_updater_transport_create_req_upload(tsprt, fw_filepath,
			dev_type, cbs, ret_req);
}

int arsdk_updater_req_upload_cancel(struct arsdk_updater_req_upload *req)
{
	struct arsdk_updater_transport *tsprt = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	tsprt = arsdk_updater_req_upload_get_transport(req);

	return arsdk_updater_transport_req_upload_cancel(tsprt, req);
}

enum arsdk_device_type arsdk_updater_appid_to_devtype(const uint32_t app_id)
{
	enum arsdk_device_type type = ARSDK_DEVICE_TYPE_UNKNOWN;

	static const struct {
		uint32_t app; /* see plf.h of tools/plf project for values */
		enum arsdk_device_type type;
	} app2devtype[] = {
		{0x68, ARSDK_DEVICE_TYPE_BEBOP},
		{0x79, ARSDK_DEVICE_TYPE_BEBOP_2},
		{0x8a, ARSDK_DEVICE_TYPE_ANAFI4K},
		{0x8d, ARSDK_DEVICE_TYPE_ANAFI_THERMAL},
		{0x89, ARSDK_DEVICE_TYPE_CHIMERA},
		{0x77, ARSDK_DEVICE_TYPE_PAROS},
		{0x8e, ARSDK_DEVICE_TYPE_ANAFI_2},
		{0x8f, ARSDK_DEVICE_TYPE_ANAFI_UA},
		{0x92, ARSDK_DEVICE_TYPE_ANAFI_USA},
		{0x67, ARSDK_DEVICE_TYPE_SKYCTRL},
		{0x81, ARSDK_DEVICE_TYPE_SKYCTRL_2},
		{0x87, ARSDK_DEVICE_TYPE_SKYCTRL_2P},
		{0x8b, ARSDK_DEVICE_TYPE_SKYCTRL_3},
		{0x90, ARSDK_DEVICE_TYPE_SKYCTRL_UA},
		{0x91, ARSDK_DEVICE_TYPE_SKYCTRL_4},
		{0x6B, ARSDK_DEVICE_TYPE_JS},
		{0x72, ARSDK_DEVICE_TYPE_JS_EVO_LIGHT},
		{0x72, ARSDK_DEVICE_TYPE_JS_EVO_RACE},
		{0x6A, ARSDK_DEVICE_TYPE_RS},
		{0x73, ARSDK_DEVICE_TYPE_RS_EVO_LIGHT},
		{0x73, ARSDK_DEVICE_TYPE_RS_EVO_BRICK},
		{0x73, ARSDK_DEVICE_TYPE_RS_EVO_HYDROFOIL},
		{0x82, ARSDK_DEVICE_TYPE_RS3},
		{0x7e, ARSDK_DEVICE_TYPE_POWERUP},
		{0x78, ARSDK_DEVICE_TYPE_EVINRUDE},
		{0x85, ARSDK_DEVICE_TYPE_WINGX},
	};
	static const size_t types_count =
			sizeof(app2devtype) / sizeof(app2devtype[0]);
	size_t i = 0;

	for (i = 0; i < types_count; i++) {
		if (app_id == app2devtype[i].app) {
			type = app2devtype[i].type;
			break;
		}
	}

	return type;
}

int arsdk_updater_read_fw_info(const char *fw_filepath,
		struct arsdk_updater_fw_info *info)
{
#ifdef BUILD_LIBPUF
	struct puf *puf = NULL;
	int res = 0;
	int fd = -1;
	uint32_t app_id = 0;

	ARSDK_RETURN_ERR_IF_FAILED(fw_filepath != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(info != NULL, -EINVAL);

	puf = puf_new(fw_filepath);
	if (puf == NULL)
		return -ENOMEM;

	res = puf_get_version(puf, &info->version);
	if (res < 0)
		goto end;

	/* get version name */
	res = puf_version_tostring(&info->version, info->name,
			sizeof(info->name));
	if (res < 0)
		goto end;

	/* get device type */
	res = puf_get_app_id(puf, &app_id);
	if (res < 0)
		goto end;
	info->devtype = arsdk_updater_appid_to_devtype(app_id);

	fd = open(fw_filepath, O_RDONLY);
	if (fd < 0) {
		res = -errno;
		goto end;
	}

	/* get size */
	res = lseek(fd, 0, SEEK_END);
	if (res < 0) {
		res = -errno;
		goto end;
	}
	info->size = res;

	/* calculate md5sum */
	res = arsdk_md5_compute(fd, info->md5);

end:
	if (fd != -1) {
		close(fd);
		fd = -1;
	}

	puf_destroy(puf);
	return res;
#else /* !BUILD_LIBPUF */
	return -ENOSYS;
#endif /* !BUILD_LIBPUF */
}

int arsdk_updater_fw_dev_comp(struct arsdk_updater_fw_info *info,
		enum arsdk_device_type dev_type)
{
	if (info == NULL)
		return 0;

	if (info->devtype == dev_type)
		return 1;

	if (info->devtype == ARSDK_DEVICE_TYPE_JS_EVO_LIGHT)
		return (dev_type == ARSDK_DEVICE_TYPE_JS_EVO_RACE) ? 1 : 0;

	if (info->devtype == ARSDK_DEVICE_TYPE_RS_EVO_LIGHT) {
		return (dev_type == ARSDK_DEVICE_TYPE_RS_EVO_BRICK ||
			dev_type == ARSDK_DEVICE_TYPE_RS_EVO_HYDROFOIL) ? 1 : 0;
	}

	return 0;
}

void *arsdk_updater_req_upload_child(struct arsdk_updater_req_upload *req)
{
	return req == NULL ? NULL : req->child;
}

struct arsdk_updater_transport *arsdk_updater_req_upload_get_transport(
		struct arsdk_updater_req_upload *req)
{
	return req == NULL ? NULL : req->tsprt;
}

enum arsdk_device_type arsdk_updater_req_upload_get_dev_type(
		const struct arsdk_updater_req_upload *req)
{
	return req == NULL ? ARSDK_DEVICE_TYPE_UNKNOWN : req->dev_type;
}


int arsdk_updater_new_req_upload(
		struct arsdk_updater_transport *tsprt,
		void *child,
		const struct arsdk_updater_req_upload_cbs *cbs,
		enum arsdk_device_type dev_type,
		struct arsdk_updater_req_upload **ret_req)
{
	struct arsdk_updater_req_upload *req = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_req != NULL, -EINVAL);
	*ret_req = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(tsprt != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(child != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->progress != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->complete != NULL, -EINVAL);

	/* Allocate structure */
	req = calloc(1, sizeof(*req));
	if (req == NULL)
		return -ENOMEM;

	/* Initialize structure */
	req->child = child;
	req->tsprt = tsprt;
	req->cbs = *cbs;
	req->dev_type = dev_type;

	*ret_req = req;
	return 0;
}

int arsdk_updater_destroy_req_upload(struct arsdk_updater_req_upload *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	free(req);
	return 0;
}
