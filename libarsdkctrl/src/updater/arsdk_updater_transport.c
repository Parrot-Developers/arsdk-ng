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
#include "updater/arsdk_updater_transport_priv.h"

/** */
struct arsdk_updater_transport {
	void                                            *child;
	struct arsdk_updater_itf                        *itf;
	const struct arsdk_updater_transport_ops        *ops;
	const char                                      *name;
};

int arsdk_updater_transport_new(void *child,
		const char *name,
		const struct arsdk_updater_transport_ops *ops,
		struct arsdk_updater_itf *itf,
		struct arsdk_updater_transport **ret_tsprt)
{
	struct arsdk_updater_transport *self = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_tsprt != NULL, -EINVAL);
	*ret_tsprt = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ops != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ops->stop != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ops->cancel_all != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ops->create_req_upload != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ops->cancel_req_upload != NULL, -EINVAL);

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Initialize structure */
	self->child = child;
	self->name = name;
	self->ops = ops;
	self->itf = itf;

	*ret_tsprt = self;
	return 0;
}

int arsdk_updater_transport_stop(struct arsdk_updater_transport *tsprt)
{
	ARSDK_RETURN_ERR_IF_FAILED(tsprt != NULL, -EINVAL);

	return (*tsprt->ops->stop)(tsprt);
}

int arsdk_updater_transport_cancel_all(struct arsdk_updater_transport *tsprt)
{
	ARSDK_RETURN_ERR_IF_FAILED(tsprt != NULL, -EINVAL);

	return (*tsprt->ops->cancel_all)(tsprt);
}

int arsdk_updater_transport_destroy(struct arsdk_updater_transport *tsprt)
{
	ARSDK_RETURN_ERR_IF_FAILED(tsprt != NULL, -EINVAL);

	arsdk_updater_transport_stop(tsprt);

	free(tsprt);
	return 0;
}

int arsdk_updater_transport_create_req_upload(
		struct arsdk_updater_transport *tsprt,
		const char *fw_filepath,
		enum arsdk_device_type dev_type,
		const struct arsdk_updater_req_upload_cbs *cbs,
		struct arsdk_updater_req_upload **ret_req)
{
	ARSDK_RETURN_ERR_IF_FAILED(tsprt != NULL, -EINVAL);

	return (*tsprt->ops->create_req_upload)(tsprt, fw_filepath, dev_type,
			cbs, ret_req);
}

int arsdk_updater_transport_req_upload_cancel(
		struct arsdk_updater_transport *tsprt,
		struct arsdk_updater_req_upload *req)
{
	ARSDK_RETURN_ERR_IF_FAILED(tsprt != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(req != NULL, -EINVAL);

	return (*tsprt->ops->cancel_req_upload)(tsprt, req);
}

void *arsdk_updater_transport_get_child(struct arsdk_updater_transport *tsprt)
{
	return tsprt == NULL ? NULL : tsprt->child;
}

struct arsdk_updater_itf *arsdk_updater_transport_get_itf(
		struct arsdk_updater_transport *tsprt)
{
	return tsprt == NULL ? NULL : tsprt->itf;
}
