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


struct arsdk_ctrl {
	/* running loop */
	struct pomp_loop              *loop;
	/* os specific user data */
	void                          *osdata;
	/* device callbacks */
	struct arsdk_ctrl_device_cbs  device_cbs;
	/* devices list */
	struct list_node              devices;
	/* backends list */
	struct list_node              backends;
	/* discoveries list */
	struct list_node              discoveries;
};

/**
 */
int arsdk_ctrl_new(struct pomp_loop *loop, struct arsdk_ctrl **ret_ctrl)
{
	struct arsdk_ctrl *ctrl = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(loop != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_ctrl != NULL, -EINVAL);
	*ret_ctrl = NULL;

	/* Allocate structure */
	ctrl = calloc(1, sizeof(*ctrl));
	if (ctrl == NULL)
		return -ENOMEM;

	srandom(time(NULL));

	/* Initialize parameters */
	ctrl->loop = loop;
	list_init(&ctrl->devices);
	list_init(&ctrl->backends);
	list_init(&ctrl->discoveries);

	*ret_ctrl = ctrl;
	return 0;
}

/**
 */
int arsdk_ctrl_set_device_cbs(struct arsdk_ctrl *self,
		const struct arsdk_ctrl_device_cbs *cbs)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->added != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->removed != NULL,
			-EINVAL);
	self->device_cbs = *cbs;
	return 0;
}

struct pomp_loop *arsdk_ctrl_get_loop(struct arsdk_ctrl *ctrl)
{
	return ctrl ? ctrl->loop : NULL;
}

/**
 */
int arsdk_ctrl_destroy(struct arsdk_ctrl *self)
{
	struct arsdk_discovery *disc, *disctmp;
	struct arsdkctrl_backend *backend, *backendtmp;
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	/* unregister all discoveries */
	list_walk_entry_forward_safe(&self->discoveries, disc, disctmp, node) {
		arsdk_ctrl_unregister_discovery(self, disc);
	}

	/* unregister all backends */
	list_walk_entry_forward_safe(&self->backends, backend, backendtmp,
			node) {
		arsdk_ctrl_unregister_backend(self, backend);
	}

	free(self);
	return 0;
}

/**
 */
static int arsdk_ctrl_register_device(struct arsdk_ctrl *self,
		struct arsdk_device *dev)
{
	struct arsdk_device *device;

	/* first check device is not already in the list */
	list_walk_entry_forward(&self->devices, device, node) {
		if (device == dev) {
			ARSDK_LOGW("can't add device %p: already added !", dev);
			return -EEXIST;
		}
	}

	/* append device in device list */
	list_add_before(&self->devices, &dev->node);

	/* notify callback */
	if (self->device_cbs.added)
		(*self->device_cbs.added) (dev, self->device_cbs.userdata);

	return 0;
}

/**
 */
static int arsdk_ctrl_unregister_device(struct arsdk_ctrl *self,
		struct arsdk_device *dev)
{
	struct arsdk_device *device;
	int found = 0;

	/* check device is in the list */
	list_walk_entry_forward(&self->devices, device, node) {
		if (device == dev) {
			found = 1;
			break;
		}
	}

	if (!found) {
		ARSDK_LOGW("can't remove device %p: not added !", dev);
		return -ENOENT;
	}

	/* remove device from list */
	list_del(&dev->node);

	/* notify callback */
	if (self->device_cbs.removed)
		(*self->device_cbs.removed) (dev, self->device_cbs.userdata);

	return 0;
}

/**
 */
struct arsdk_device *
arsdk_ctrl_next_device(struct arsdk_ctrl *ctrl, struct arsdk_device *prev)
{
	struct list_node *node;
	struct arsdk_device *next;

	if (!ctrl)
		return NULL;

	node = list_next(&ctrl->devices, prev ? &prev->node : &ctrl->devices);
	if (!node)
		return NULL;

	next = list_entry(node, struct arsdk_device, node);
	return next;
}

static uint16_t arsdk_ctrl_generate_device_handle(struct arsdk_ctrl *self)
{
	struct arsdk_device *dev;
	int collision;
	uint16_t handle;

	while (1) {
		/* generate random handle */
		handle = (uint16_t)random();
		if (handle == ARSDK_INVALID_HANDLE)
			continue;

		/* check for device handle collision */
		collision = 0;
		list_walk_entry_forward(&self->devices, dev, node) {
			if (arsdk_device_get_handle(dev) == handle) {
				collision = 1;
				break;
			}
		}

		if (!collision)
			break;
	}

	return handle;
}


/**
 */
int arsdk_ctrl_create_device(struct arsdk_ctrl *self,
		struct arsdk_discovery *discovery,
		int16_t discovery_runid,
		const struct arsdk_device_info *info,
		struct arsdk_device **ret_obj)
{
	struct arsdk_device *dev = NULL;
	uint16_t handle;
	int res;
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(discovery != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(info != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;

	/* generate a new handle */
	handle = arsdk_ctrl_generate_device_handle(self);

	/* create the device */
	res = arsdk_device_new(discovery->backend, discovery, discovery_runid,
				handle, info, &dev);
	if (res < 0)
		return res;

	/* register it in manager */
	res = arsdk_ctrl_register_device(self, dev);
	if (res < 0) {
		arsdk_device_destroy(dev);
		return res;
	}

	*ret_obj = dev;
	return 0;
}

/**
 */
int arsdk_ctrl_destroy_device(struct arsdk_ctrl *self,
		struct arsdk_device *dev)
{
	const struct arsdk_device_info *devinfo;
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(dev != NULL, -EINVAL);

	/* get device info */
	arsdk_device_get_info(dev, &devinfo);

	/* mark device has deleted so app can not start
	 * a new connection on it */
	dev->deleted = 1;

	switch (devinfo->state) {
	case ARSDK_DEVICE_STATE_CONNECTED:
	case ARSDK_DEVICE_STATE_CONNECTING:
		/* force device disconnection */
		ARSDK_LOGI("internally disconnect device "
			"name='%s' type=%s id='%s'",
			devinfo->name, arsdk_device_type_str(devinfo->type),
			devinfo->id);
		arsdk_device_disconnect(dev);
	break;
	case ARSDK_DEVICE_STATE_REMOVING:
	case ARSDK_DEVICE_STATE_IDLE:
	default:
		/* nothing to do */
	break;
	}

	/* unregister it from manager */
	arsdk_ctrl_unregister_device(self, dev);

	/* destroy device */
	arsdk_device_destroy(dev);
	return 0;
}

int arsdk_ctrl_register_backend(struct arsdk_ctrl *self,
		struct arsdkctrl_backend *backend)
{
	struct arsdkctrl_backend *b;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(backend != NULL, -EINVAL);

	/* first check backend is not already in the list */
	list_walk_entry_forward(&self->backends, b, node) {
		if (b == backend) {
			ARSDK_LOGW("can't register backend %p:"
				"already registered !", backend);
			return -EEXIST;
		}
	}

	/* add backend in list */
	list_add_before(&self->backends, &backend->node);
	return 0;
}

int arsdk_ctrl_unregister_backend(struct arsdk_ctrl *self,
		struct arsdkctrl_backend *backend)
{
	struct arsdkctrl_backend *b;
	struct arsdk_device *dev, *tmpdev;
	int found = 0;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(backend != NULL, -EINVAL);

	/* check backend is in the list */
	list_walk_entry_forward(&self->backends, b, node) {
		if (b == backend) {
			found = 1;
			break;
		}
	}

	if (!found) {
		ARSDK_LOGW("can't unregister backend %p: not registered !",
				backend);
		return -ENOENT;
	}

	/* remove all devices from backend */
	list_walk_entry_forward_safe(&self->devices, dev, tmpdev, node) {
		if (dev->backend == backend)
			arsdk_ctrl_destroy_device(self, dev);
	}

	/* remove backend from list */
	list_del(&backend->node);

	return 0;
}

int arsdk_ctrl_register_discovery(struct arsdk_ctrl *self,
		struct arsdk_discovery *discovery)
{
	struct arsdk_discovery *d;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(discovery != NULL, -EINVAL);

	/* first check discovery is not already in the list */
	list_walk_entry_forward(&self->discoveries, d, node) {
		if (d == discovery) {
			ARSDK_LOGW("can't register discovery %p:"
				"already registered !", discovery);
			return -EEXIST;
		}
	}

	/* add discovery in list */
	list_add_before(&self->discoveries, &discovery->node);
	return 0;
}

int arsdk_ctrl_unregister_discovery(struct arsdk_ctrl *self,
		struct arsdk_discovery *discovery)
{
	struct arsdk_discovery *d;
	struct arsdk_device *dev;
	int found = 0;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(discovery != NULL, -EINVAL);

	/* check discovery is in the list */
	list_walk_entry_forward(&self->discoveries, d, node) {
		if (d == discovery) {
			found = 1;
			break;
		}
	}

	if (!found) {
		ARSDK_LOGW("can't unregister discovery %p: not registered !",
				discovery);
		return -ENOENT;
	}

	/* remove discovery from list */
	list_del(&discovery->node);

	/* clear all devices discovery info */
	list_walk_entry_forward(&self->devices, dev, node) {
		if (arsdk_device_get_discovery(dev) == discovery)
			arsdk_device_clear_discovery(dev);
	}

	return 0;
}

struct arsdk_device *arsdk_ctrl_get_device(struct arsdk_ctrl *self,
		uint16_t handle)
{
	struct arsdk_device *dev;

	if (!self || handle == ARSDK_INVALID_HANDLE)
		return NULL;

	list_walk_entry_forward(&self->devices, dev, node) {
		if (arsdk_device_get_handle(dev) == handle)
			return dev;
	}

	return NULL;
}
