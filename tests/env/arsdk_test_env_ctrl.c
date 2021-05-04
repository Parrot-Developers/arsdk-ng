/**
 * Copyright (c) 2020 Parrot Drones SAS
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

#include "arsdk_test.h"
#include "arsdk_test_env_mux_tip.h"
#include "arsdk_test_env_ctrl.h"


#define LOG_TAG "arsdk_test_env_ctrl"
#include "arsdk_test_log.h"

struct arsdk_test_env_ctrl {
	struct pomp_loop             *loop;
	struct arsdk_ctrl            *ctrl;
	enum arsdk_backend_type      backend_type;
	struct {
		struct {
			struct arsdkctrl_backend_net    *backend;
			struct arsdk_discovery_net      *discovery;
		} net;

		struct {
			struct arsdk_test_env_mux_tip *client;
			struct arsdk_discovery_cfg discovery_cfg;
			struct arsdkctrl_backend_mux *backend;
			struct arsdk_discovery_mux *discovery;
		} mux;
	} transport;
	struct arsdk_device          *device;
	struct arsdk_cmd_itf         *cmd_itf;

	struct arsdk_device_conn_cbs cbs;
};


/**
 */
static void connecting(struct arsdk_device *device,
		const struct arsdk_device_info *info,
		void *userdata)
{
	struct arsdk_test_env_ctrl *self = userdata;
	CU_ASSERT_PTR_NOT_NULL_FATAL(self);

	TST_LOG_FUNC();

	if (self->cbs.connecting != NULL)
		(self->cbs.connecting)(device, info, self->cbs.userdata);
}

/**
 */
static void connected(struct arsdk_device *device,
		const struct arsdk_device_info *info,
		void *userdata)
{
	struct arsdk_test_env_ctrl *self = userdata;
	CU_ASSERT_PTR_NOT_NULL_FATAL(self);

	TST_LOG_FUNC();

	if (self->cbs.connected != NULL)
		(self->cbs.connected)(device, info, self->cbs.userdata);
}

/**
 */
static void canceled(struct arsdk_device *device,
		const struct arsdk_device_info *info,
		enum arsdk_conn_cancel_reason reason,
		void *userdata)
{
	struct arsdk_test_env_ctrl *self = userdata;
	CU_ASSERT_PTR_NOT_NULL_FATAL(self);

	TST_LOG_FUNC();

	if (self->cbs.canceled != NULL)
		(self->cbs.canceled)(device, info, reason , self->cbs.userdata);
}

/**
 */
static void disconnected(struct arsdk_device *device,
		const struct arsdk_device_info *info,
		void *userdata)
{
	struct arsdk_test_env_ctrl *self = userdata;
	CU_ASSERT_PTR_NOT_NULL_FATAL(self);

	TST_LOG_FUNC();

	if (self->cbs.disconnected != NULL)
		(self->cbs.disconnected)(device, info, self->cbs.userdata);
}

/**
 */
static void link_status(struct arsdk_device *device,
		const struct arsdk_device_info *info,
		enum arsdk_link_status status,
		void *userdata)
{
	struct arsdk_test_env_ctrl *self = userdata;
	CU_ASSERT_PTR_NOT_NULL_FATAL(self);

	TST_LOG("%s: status=%s", __func__, arsdk_link_status_str(status));

	if (self->cbs.link_status != NULL)
		(self->cbs.link_status)(device, info, status,
				self->cbs.userdata);

	if (status == ARSDK_LINK_STATUS_KO)
		arsdk_device_disconnect(device);
}

/**
 */
static void device_added(struct arsdk_device *device, void *userdata)
{
	TST_LOG_FUNC();

	struct arsdk_test_env_ctrl *self = userdata;
	CU_ASSERT_PTR_NOT_NULL_FATAL(self);

	/* Only interested in first device found */
	CU_ASSERT_PTR_NULL_FATAL(self->device);

	/* Save device */
	self->device = device;

	/* Connect to device */
	struct arsdk_device_conn_cfg cfg = {
		.ctrl_name = "controller",
		.ctrl_type = "test",
	};

	struct arsdk_device_conn_cbs cbs = {
		.userdata = self,
		.connecting = &connecting,
		.connected = &connected,
		.disconnected = &disconnected,
		.canceled = &canceled,
		.link_status = &link_status,
	};

	int res = arsdk_device_connect(device, &cfg, &cbs, self->loop);
	CU_ASSERT_EQUAL_FATAL(res, 0);
}

/**
 */
static void device_removed(struct arsdk_device *device, void *userdata)
{
	TST_LOG_FUNC();

	struct arsdk_test_env_ctrl *self = userdata;
	CU_ASSERT_PTR_NOT_NULL_FATAL(self);

	CU_ASSERT_EQUAL(device, self->device);
	self->device = NULL;
}

/**
 */
static void socket_cb(struct arsdkctrl_backend_net *self, int fd,
		enum arsdk_socket_kind kind, void *userdata)
{
	TST_LOG("socket_cb :self:%p fd:%d kind:%s userdata:%p",
			self, fd, arsdk_socket_kind_str(kind), userdata);
}

/**
 */
static void backend_create_net(struct arsdk_test_env_ctrl *self,
		struct arsdk_discovery_cfg discovery_cfg)
{
	TST_LOG_FUNC();

	int res = 0;
	struct arsdkctrl_backend_net_cfg backend_net_cfg = {
		.stream_supported = 1,
	};
	res = arsdkctrl_backend_net_new(self->ctrl, &backend_net_cfg,
			&self->transport.net.backend);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	res = arsdkctrl_backend_net_set_socket_cb(self->transport.net.backend,
			&socket_cb, self);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	/* Start net discovery */
	res = arsdk_discovery_net_new(self->ctrl,
			self->transport.net.backend, &discovery_cfg,
			"127.0.0.1",
			&self->transport.net.discovery);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	res = arsdk_discovery_net_start(self->transport.net.discovery);
	CU_ASSERT_EQUAL_FATAL(res, 0);
}

static void backend_destroy_net(struct arsdk_test_env_ctrl *self)
{
	int res;
	TST_LOG_FUNC();

	if (self->transport.net.discovery) {
		res = arsdk_discovery_net_stop(self->transport.net.discovery);
		CU_ASSERT_EQUAL_FATAL(res, 0);

		res = arsdk_discovery_net_destroy(self->transport.net.discovery);
		CU_ASSERT_EQUAL_FATAL(res, 0);
		self->transport.net.discovery = NULL;
	}

	if (self->transport.net.backend != NULL) {
		res = arsdkctrl_backend_net_destroy(self->transport.net.backend);
		CU_ASSERT_EQUAL_FATAL(res, 0);
		self->transport.net.backend = NULL;
	}
}

static void on_mux_connect(struct arsdk_test_env_mux_tip *mux_tip, void *userdata)
{
	TST_LOG_FUNC();

	struct arsdk_test_env_ctrl *self = userdata;
	CU_ASSERT_PTR_NOT_NULL_FATAL(self);

	struct mux_ctx *mctx = arsdk_test_env_mux_tip_get_mctx(
			self->transport.mux.client);
	CU_ASSERT_PTR_NOT_NULL_FATAL(mctx);

	/* Create mux backend */
	struct arsdkctrl_backend_mux_cfg backend_mux_cfg = {
		.mux = mctx,
	};
	int res = arsdkctrl_backend_mux_new(self->ctrl, &backend_mux_cfg,
			&self->transport.mux.backend);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	/* Create mux discovery */
	res = arsdk_discovery_mux_new(self->ctrl,
			self->transport.mux.backend, &self->transport.mux.discovery_cfg,
			mctx, &self->transport.mux.discovery);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	/* Start mux discovery */
	res = arsdk_discovery_mux_start(self->transport.mux.discovery);
	CU_ASSERT_EQUAL_FATAL(res, 0);
}

static void on_mux_disconnect(struct arsdk_test_env_mux_tip *mux_tip, void *userdata)
{
	TST_LOG_FUNC();
}

/**
 */
static void backend_create_mux(struct arsdk_test_env_ctrl *self,
		struct arsdk_discovery_cfg *discovery_cfg)
{
	TST_LOG_FUNC();

	self->transport.mux.discovery_cfg = *discovery_cfg;

	struct arsdk_test_env_mux_tip_cbs tip_cbs = {
		.on_connect = &on_mux_connect,
		.on_disconnect = &on_mux_disconnect,
		.userdata = self,
	};

	/* Create mux client */
	int res = arsdk_test_env_mux_tip_new(self->loop,
			ARSDK_TEST_ENV_MUX_TIP_TYPE_CLIENT, &tip_cbs,
			&self->transport.mux.client);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	res = arsdk_test_env_mux_tip_start(self->transport.mux.client);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	/* Wait mux connection */
}

static void backend_destroy_mux(struct arsdk_test_env_ctrl *self)
{
	int res;
	TST_LOG_FUNC();

	if (self->transport.mux.discovery) {
		res = arsdk_discovery_mux_stop(self->transport.mux.discovery);
		CU_ASSERT_EQUAL_FATAL(res, 0);

		res = arsdk_discovery_mux_destroy(self->transport.mux.discovery);
		CU_ASSERT_EQUAL_FATAL(res, 0);
		self->transport.mux.discovery = NULL;
	}

	if (self->transport.mux.backend != NULL) {
		res = arsdkctrl_backend_mux_destroy(self->transport.mux.backend);
		CU_ASSERT_EQUAL_FATAL(res, 0);
		self->transport.mux.backend = NULL;
	}

	if (self->transport.mux.client != NULL) {
		res = arsdk_test_env_mux_tip_stop(self->transport.mux.client);
		CU_ASSERT_EQUAL_FATAL(res, 0);

		arsdk_test_env_mux_tip_destroy(self->transport.mux.client);
		self->transport.mux.client = NULL;
	}
}


/**
 */
static void backend_create(struct arsdk_test_env_ctrl *self)
{

	TST_LOG_FUNC();

	struct arsdk_discovery_cfg discovery_cfg;
	static const enum arsdk_device_type types[] = {
		ARSDK_DEVICE_TYPE_ANAFI_2,
	};
	memset(&discovery_cfg, 0, sizeof(discovery_cfg));
	discovery_cfg.types = types;
	discovery_cfg.count = sizeof(types) / sizeof(types[0]);

	switch (self->backend_type) {
	case ARSDK_BACKEND_TYPE_NET:
		backend_create_net(self, discovery_cfg);

		break;
	case ARSDK_BACKEND_TYPE_MUX:
		backend_create_mux(self, &discovery_cfg);
		break;

	default:
		CU_FAIL_FATAL("Unsupported backend");
		return;
	}

	/* wait for connection request */
}

static void backend_destroy(struct arsdk_test_env_ctrl *self)
{
	int res;
	TST_LOG_FUNC();

	backend_destroy_net(self);

	backend_destroy_mux(self);

	if (self->device != NULL) {
		res = arsdk_device_disconnect(self->device);
		CU_ASSERT_EQUAL_FATAL(res, 0);
		/* device should be NULL */
		CU_ASSERT_PTR_NULL_FATAL(self->device);
	}
}

int arsdk_test_env_ctrl_new(struct pomp_loop *loop,
		enum arsdk_backend_type backend_type,
		struct arsdk_device_conn_cbs *cbs,
		struct arsdk_test_env_ctrl **ret_ctrl)
{
	TST_LOG_FUNC();

	struct arsdk_test_env_ctrl *self = NULL;

	self = calloc(1, sizeof(*self));
	CU_ASSERT_PTR_NOT_NULL_FATAL(self);

	self->backend_type = backend_type;
	self->loop = loop;
	self->cbs = *cbs;

	/* Create manager */
	int res = arsdk_ctrl_new(loop, &self->ctrl);
	CU_ASSERT_PTR_NOT_NULL_FATAL(self->ctrl);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	/* Create device manager */
	struct arsdk_ctrl_device_cbs ctrl_device_cbs = {
		.userdata = self,
		.added = &device_added,
		.removed = &device_removed,
	};
	res = arsdk_ctrl_set_device_cbs(self->ctrl, &ctrl_device_cbs);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	backend_create(self);

	*ret_ctrl = self;
	return 0;
}

int arsdk_test_env_ctrl_destroy(struct arsdk_test_env_ctrl *self)
{
	TST_LOG_FUNC();

	CU_ASSERT_PTR_NOT_NULL_FATAL(self);

	backend_destroy(self);

	int res = arsdk_ctrl_destroy(self->ctrl);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	free(self);
	return 0;
}