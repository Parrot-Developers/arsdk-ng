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

#include "arsdk_test.h"
#include "arsdk_test_protoc.h"
#include <arsdk/arsdk.h>
#include <arsdk/internal/arsdk_internal.h>

/* CONTROLLER PART */

/** */
struct test_ctrl {
	struct pomp_loop             *loop;
	struct arsdk_ctrl            *ctrl;
	struct arsdkctrl_backend_net *backend_net;
	struct arsdk_discovery_net   *discovery_net;
	struct arsdk_device          *device;
	struct arsdk_cmd_itf         *cmd_itf;
};

/** */
static struct test_ctrl s_ctrl = {
	.loop = NULL,
	.ctrl = NULL,
	.backend_net = NULL,
	.discovery_net = NULL,
	.device = NULL,
	.cmd_itf = NULL,
};

/**
 */
static void ctrl_send_status(struct arsdk_cmd_itf *itf,
		const struct arsdk_cmd *cmd,
		enum arsdk_cmd_buffer_type type,
		enum arsdk_cmd_itf_cmd_send_status status,
		uint16_t seq,
		int done,
		void *userdata)
{

}

/**
 */
static void ctrl_recv_cmd(struct arsdk_cmd_itf *itf,
		const struct arsdk_cmd *cmd,
		void *userdata)
{

}

/**
 */
static void ctrl_connecting(struct arsdk_device *device,
		const struct arsdk_device_info *info,
		void *userdata)
{

}

/**
 */
static void ctrl_connected(struct arsdk_device *device,
		const struct arsdk_device_info *info,
		void *userdata)
{
	int res = 0;
	struct test_ctrl *ctrl = userdata;
	struct arsdk_cmd_itf_cbs cmd_cbs;

	fprintf(stderr, "controller connected\n");

	/* Create command interface object */
	memset(&cmd_cbs, 0, sizeof(cmd_cbs));
	cmd_cbs.userdata = ctrl;
	cmd_cbs.recv_cmd = &ctrl_recv_cmd;
	cmd_cbs.cmd_send_status = &ctrl_send_status;
	res = arsdk_device_create_cmd_itf(device, &cmd_cbs, &ctrl->cmd_itf);
	CU_ASSERT_EQUAL(res, 0);

	test_connected();
}

/**
 */
static void ctrl_disconnected(struct arsdk_device *device,
		const struct arsdk_device_info *info,
		void *userdata)
{
	struct test_ctrl *ctrl = userdata;

	fprintf(stderr, "controller disconnected\n");

	ctrl->device = NULL;
}

/**
 */
static void canceled(struct arsdk_device *device,
		const struct arsdk_device_info *info,
		enum arsdk_conn_cancel_reason reason,
		void *userdata)
{
	struct test_ctrl *ctrl = userdata;
	ctrl->device = NULL;
}

/**
 */
static void link_status(struct arsdk_device *device,
		const struct arsdk_device_info *info,
		enum arsdk_link_status status,
		void *userdata)
{
	if (status == ARSDK_LINK_STATUS_KO) {
		arsdk_device_disconnect(device);
	}
}

/**
 */
static void device_added(struct arsdk_device *device, void *userdata)
{
	int res = 0;
	struct test_ctrl *ctrl = userdata;
	struct arsdk_device_conn_cfg cfg;
	struct arsdk_device_conn_cbs cbs;
	const struct arsdk_device_info *info = NULL;

	CU_ASSERT_NOT_EQUAL(device, NULL);
	CU_ASSERT_NOT_EQUAL(ctrl, NULL);

	/* Get device info */
	res = arsdk_device_get_info(device, &info);
	CU_ASSERT_EQUAL(res, 0);

	/* Only interested in first device found */
	if (ctrl->device != NULL)
		return;

	/* Check device name */
	if (strcmp(info->name, DEVICE_NAME) != 0) {
		/* Is not the device searched */
		return;
	}

	/* Save device */
	ctrl->device = device;

	/* Connect to device */
	memset(&cfg, 0, sizeof(cfg));
	cfg.ctrl_name = "unitest_ctrl";
	cfg.ctrl_type = "unitest";
	memset(&cbs, 0, sizeof(cbs));
	cbs.userdata = ctrl;
	cbs.connecting = &ctrl_connecting;
	cbs.connected = &ctrl_connected;
	cbs.disconnected = &ctrl_disconnected;
	cbs.canceled = &canceled;
	cbs.link_status = &link_status;
	res = arsdk_device_connect(device, &cfg, &cbs, ctrl->loop);
	CU_ASSERT_EQUAL(res, 0);
}

/**
 */
static void device_removed(struct arsdk_device *device, void *userdata)
{

}

/**
 */
static void test_create_ctrl_backend(struct test_ctrl *ctrl)
{
	int res = 0;
	struct arsdkctrl_backend_net_cfg backend_net_cfg;
	struct arsdk_discovery_cfg discovery_cfg;
	static const enum arsdk_device_type types[] = {
		ARSDK_DEVICE_TYPE_BEBOP,
		ARSDK_DEVICE_TYPE_BEBOP_2,
		ARSDK_DEVICE_TYPE_EVINRUDE,
		ARSDK_DEVICE_TYPE_JS,
	};

	memset(&backend_net_cfg, 0, sizeof(backend_net_cfg));
	res = arsdkctrl_backend_net_new(ctrl->ctrl, &backend_net_cfg,
			&ctrl->backend_net);
	CU_ASSERT_EQUAL(res, 0);

	memset(&discovery_cfg, 0, sizeof(discovery_cfg));
	discovery_cfg.types = types;
	discovery_cfg.count = sizeof(types) / sizeof(types[0]);

	/* start net discovery */
	res = arsdk_discovery_net_new(ctrl->ctrl,
			ctrl->backend_net, &discovery_cfg, "127.0.0.1",
			&ctrl->discovery_net);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_discovery_net_start(ctrl->discovery_net);
	CU_ASSERT_EQUAL(res, 0);
}

/**
 */
static void backend_ctrl_destroy(struct test_ctrl *ctrl)
{
	int res = 0;

	if (ctrl->discovery_net) {
		res = arsdk_discovery_net_stop(ctrl->discovery_net);
		CU_ASSERT_EQUAL(res, 0);

		arsdk_discovery_net_destroy(ctrl->discovery_net);
		ctrl->discovery_net = NULL;
	}

	if (ctrl->device != NULL) {
		res = arsdk_device_disconnect(ctrl->device);
		CU_ASSERT_EQUAL(res, 0);
		CU_ASSERT_EQUAL(ctrl->device, NULL);

		ctrl->device = NULL;
	}

	if (ctrl->backend_net != NULL) {
		res = arsdkctrl_backend_net_destroy(ctrl->backend_net);
		CU_ASSERT_EQUAL(res, 0);

		ctrl->backend_net = NULL;
	}
}

/** */
static void test_create_ctrl_ctrl(struct test_ctrl *ctrl)
{
	int res = 0;
	struct arsdk_ctrl_device_cbs ctrl_device_cbs;

	/* Create device manager */
	memset(&ctrl_device_cbs, 0, sizeof(ctrl_device_cbs));
	ctrl_device_cbs.userdata = ctrl;
	ctrl_device_cbs.added = &device_added;
	ctrl_device_cbs.removed = &device_removed;
	res = arsdk_ctrl_new(ctrl->loop, &ctrl->ctrl);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_ctrl_set_device_cbs(ctrl->ctrl, &ctrl_device_cbs);
	CU_ASSERT_EQUAL(res, 0);
}

/**
 */
void test_create_ctrl(struct pomp_loop *loop, struct test_ctrl **ctrl)
{
	/*create copy loop pomp*/
	s_ctrl.loop = loop;

	test_create_ctrl_ctrl(&s_ctrl);
	test_create_ctrl_backend(&s_ctrl);

	/*return controller*/
	*ctrl = &s_ctrl;
}

/**
 */
void test_delete_ctrl(struct test_ctrl *ctrl)
{
	int res = 0;

	backend_ctrl_destroy(ctrl);

	res = arsdk_ctrl_destroy(ctrl->ctrl);
	CU_ASSERT_EQUAL(res, 0);
}

struct arsdk_cmd_itf *test_ctrl_get_itf(struct test_ctrl *ctrl)
{
	return ctrl->cmd_itf;
}
