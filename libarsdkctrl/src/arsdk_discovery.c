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

#define DISCOVERY_TIMEOUT 5000

/**
 */
int arsdk_discovery_start(struct arsdk_discovery *self)
{
	int ret;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	ARSDK_LOGI("discovery '%s': start", self->name);

	/* check if already started */
	if (self->started)
		return -EBUSY;

	/* increment discovery runid */
	self->runid++;

	/* start discovery timer */
	ret = pomp_timer_set(self->timer, DISCOVERY_TIMEOUT);
	if (ret < 0)
		ARSDK_LOGE("pomp_timer_set error:%s", strerror(errno));

	/* set discovery started */
	self->started = 1;
	return 0;
}

/**
 */
int arsdk_discovery_stop(struct arsdk_discovery *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	ARSDK_LOGI("discovery '%s': stop", self->name);

	/* check if already started */
	if (!self->started)
		return -ENOENT;

	/* clear discovery time */
	pomp_timer_clear(self->timer);
	self->started = 0;
	return 0;
}

/**
 */
static void arsdk_discovery_timer_cb(struct pomp_timer *timer, void *userdata)
{
	struct arsdk_discovery *self = userdata;
	const struct arsdk_device_info *devinfo;
	struct arsdk_device *dev = NULL;

	ARSDK_LOGD("discovery '%s': timer raised", self->name);

	/* remove all devices previously discovered */
	while (1) {
		/* get next device */
		dev = arsdk_ctrl_next_device(self->ctrl, dev);
		if (!dev)
			break;

		/* ignore device not discovered by this discovery */
		if (arsdk_device_get_discovery(dev) != self)
			continue;

		/* check if device has been discovered by a previous run */
		if (arsdk_device_get_discovery_runid(dev) == self->runid)
			continue;

		/* check device type is same */
		 arsdk_device_get_info(dev, &devinfo);

		/* destroy device */
		ARSDK_LOGI("discovery '%s': remove device on timeout "
			"name='%s' id='%s'", self->name, devinfo->name,
			devinfo->id);
		arsdk_ctrl_destroy_device(self->ctrl, dev);
		dev = NULL;
	}
}

static struct arsdk_device *arsdk_discovery_find_device(
		struct arsdk_discovery *self,
		const struct arsdk_discovery_device_info *info)
{
	struct arsdk_device *dev = NULL;
	const struct arsdk_device_info *devinfo;

	/* find device */
	while (1) {
		/* get next device */
		dev = arsdk_ctrl_next_device(self->ctrl, dev);
		if (!dev)
			break;

		/* ignore device not discovered by this discovery */
		if (arsdk_device_get_discovery(dev) != self)
			continue;

		/* check device type is same */
		 arsdk_device_get_info(dev, &devinfo);
		if (info->type != devinfo->type)
			continue;

		/* compare device id if valid */
		if (info->id && (info->id[0] != '\0')) {
			if (strcmp(devinfo->id, info->id) == 0)
				break;
		/* compare device name if id is invalid */
		} else if (strcmp(devinfo->name, info->name) == 0) {
			break;
		}
	}

	return dev;
}

/**
 */
int arsdk_discovery_add_device(struct arsdk_discovery *self,
		const struct arsdk_discovery_device_info *info)
{
	struct arsdk_device_info devinfo;
	struct arsdk_device *dev;
	int ret;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(info != NULL, -EINVAL);

	/* check if device already exists */
	dev = arsdk_discovery_find_device(self, info);
	if (dev) {
		ARSDK_LOGD("discovery '%s': add device name='%s' id='%s' "
			"already added", self->name, info->name, info->id);
		/* update device discovery runid */
		arsdk_device_set_discovery_runid(dev, self->runid);
		return 0;
	}

	/* device doesn't exist create it */
	memset(&devinfo, 0, sizeof(devinfo));
	devinfo.name = info->name;
	devinfo.type = info->type;
	devinfo.addr = info->addr;
	devinfo.id = info->id;
	devinfo.port = info->port;
	devinfo.api = info->api;

	/* create device */
	ARSDK_LOGI("discovery '%s': add device name='%s' id='%s'",
			self->name, info->name, info->id);
	ret = arsdk_ctrl_create_device(self->ctrl, self, self->runid,
			&devinfo, &dev);
	if (ret < 0) {
		ARSDK_LOG_ERRNO("arsdk_ctrl_create_device", -ret);
		return ret;
	}

	return 0;
}

/**
 */
int arsdk_discovery_remove_device(struct arsdk_discovery *self,
		const struct arsdk_discovery_device_info *info)
{
	struct arsdk_device *dev;
	int ret;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(info != NULL, -EINVAL);

	/* find device previously seen by this discovery */
	dev = arsdk_discovery_find_device(self, info);
	if (!dev) {
		ARSDK_LOGW("discovery '%s': remove device name='%s' id='%s' "
			"not found", self->name, info->name, info->id);
		return -ENOENT;
	}

	/* remove device from manager */
	ARSDK_LOGI("discovery '%s': remove device name='%s' id='%s'",
			self->name, info->name, info->id);

	ret = arsdk_ctrl_destroy_device(self->ctrl, dev);
	if (ret < 0) {
		ARSDK_LOG_ERRNO("arsdk_ctrl_destroy_device", -ret);
		return ret;
	}

	return 0;
}

/**
 */
int arsdk_discovery_new(const char *name,
		struct arsdkctrl_backend *backend,
		struct arsdk_ctrl *ctrl,
		struct arsdk_discovery **ret_obj)
{
	struct arsdk_discovery *self = NULL;
	int ret;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(name != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(name[0] != '\0', -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(backend != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ctrl != NULL, -EINVAL);

	/* allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* initialize structure */
	self->backend = backend;
	self->name = xstrdup(name);
	self->ctrl = ctrl;
	self->runid = 0;

	/* create purge discovery timer */
	self->timer = pomp_timer_new(arsdk_ctrl_get_loop(self->ctrl),
			&arsdk_discovery_timer_cb, self);
	if (!self->timer) {
		ret = -EINVAL;
		goto error;
	}

	/* register discovery in manager */
	ret = arsdk_ctrl_register_discovery(self->ctrl, self);
	if (ret < 0)
		goto error;

	*ret_obj = self;
	return 0;

error:
	arsdk_discovery_destroy(self);
	return ret;
}

/**
 */
int arsdk_discovery_destroy(struct arsdk_discovery *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	/* unregister discovery from manager */
	arsdk_ctrl_unregister_discovery(self->ctrl, self);

	pomp_timer_destroy(self->timer);
	free(self->name);
	free(self);
	return 0;
}

