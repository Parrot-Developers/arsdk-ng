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
/**
 */
int arsdkctrl_backend_new(void *child, struct arsdk_ctrl *ctrl,
		const char *name,
		enum arsdk_backend_type type,
		const struct arsdkctrl_backend_ops *ops,
		struct arsdkctrl_backend **ret_obj)
{
	struct arsdkctrl_backend *self = NULL;
	int ret;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(ops != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ctrl != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(name != NULL, -EINVAL);

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Initialize structure */
	self->child = child;
	self->ops = ops;
	self->name = strdup(name);
	self->type = type;
	self->ctrl = ctrl;

	/* register backend in manager */
	ret = arsdk_ctrl_register_backend(self->ctrl, self);
	if (ret < 0) {
		free(self->name);
		free(self);
		return ret;
	}

	*ret_obj = self;
	return 0;
}

/**
 */
int arsdkctrl_backend_destroy(struct arsdkctrl_backend *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	/* unregister backend from manager */
	arsdk_ctrl_unregister_backend(self->ctrl, self);
	free(self->name);
	free(self);
	return 0;
}

/**
 */
void *arsdkctrl_backend_get_child(struct arsdkctrl_backend *self)
{
	return self == NULL ? NULL : self->child;
}

/**
 */
int arsdkctrl_backend_set_osdata(struct arsdkctrl_backend *self, void *osdata)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	self->osdata = osdata;
	return 0;
}

/**
 */
void *arsdkctrl_backend_get_osdata(struct arsdkctrl_backend *self)
{
	return self == NULL ? NULL : self->osdata;
}

/**
 */
const char *arsdkctrl_backend_get_name(struct arsdkctrl_backend *self)
{
	return self ? self->name : NULL;
}

/**
 */
enum arsdk_backend_type arsdkctrl_backend_get_type(
		struct arsdkctrl_backend *self)
{
	return self ? self->type : ARSDK_BACKEND_TYPE_UNKNOWN;
}

/**
 */
int arsdkctrl_backend_start_device_conn(struct arsdkctrl_backend *self,
		struct arsdk_device *device,
		struct arsdk_device_info *info,
		const struct arsdk_device_conn_cfg *cfg,
		const struct arsdk_device_conn_internal_cbs *cbs,
		struct pomp_loop *loop,
		struct arsdk_device_conn **ret_conn)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	if (self->ops->start_device_conn == NULL)
		return -ENOSYS;
	return (*self->ops->start_device_conn)(self,
			device, info, cfg, cbs, loop, ret_conn);
}

/**
 */
int arsdkctrl_backend_stop_device_conn(struct arsdkctrl_backend *self,
		struct arsdk_device *device,
		struct arsdk_device_conn *conn)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	if (self->ops->stop_device_conn == NULL)
		return -ENOSYS;
	return (*self->ops->stop_device_conn)(self, device, conn);
}


int arsdkctrl_backend_socket_cb(struct arsdkctrl_backend *self, int fd,
		enum arsdk_socket_kind kind)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	/* socket hook callback */
	if (self->ops->socket_cb != NULL)
		(*self->ops->socket_cb)(self, fd, kind);

	return 0;
}
