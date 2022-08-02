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

/* DEVICE PART */

/** */
struct test_dev {
	struct pomp_loop             *loop;
	struct arsdk_mngr            *mngr;
	struct arsdk_backend_net     *backend_net;
	struct arsdk_publisher_net   *publisher_net;
	struct arsdk_peer            *peer;
	struct arsdk_cmd_itf         *cmd_itf;
};

/** */
static struct test_dev s_dev = {
	.loop = NULL,
	.mngr = NULL,
	.backend_net = NULL,
	.publisher_net = NULL,
	.peer = NULL,
	.cmd_itf = NULL,
};

/**
 */
static void dev_send_status(struct arsdk_cmd_itf *itf,
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
static void dev_recv_cmd(struct arsdk_cmd_itf *itf,
		const struct arsdk_cmd *cmd,
		void *userdata)
{
	CU_ASSERT_PTR_NOT_NULL(itf);
	CU_ASSERT_PTR_NOT_NULL(cmd);
	CU_ASSERT_PTR_NOT_NULL(userdata);

	test_dev_recv_cmd(cmd);
}

/**
 */
static void dev_connected(struct arsdk_peer *peer,
		const struct arsdk_peer_info *info,
		void *userdata)
{
	int res = 0;
	struct test_dev *dev = userdata;
	struct arsdk_cmd_itf_cbs cmd_cbs;

	fprintf(stderr, "device connected\n");

	/* Create command interface object */
	memset(&cmd_cbs, 0, sizeof(cmd_cbs));
	cmd_cbs.userdata = dev;
	cmd_cbs.recv_cmd = &dev_recv_cmd;
	cmd_cbs.cmd_send_status = &dev_send_status;
	res = arsdk_peer_create_cmd_itf(peer, &cmd_cbs, &dev->cmd_itf);
	CU_ASSERT_EQUAL(res, 0);
}

/**
 */
static void dev_disconnected(struct arsdk_peer *peer,
		const struct arsdk_peer_info *info,
		void *userdata)
{
	struct test_dev *dev = userdata;

	fprintf(stderr, "device disconnected\n");

	dev->cmd_itf = NULL;
	dev->peer = NULL;
}

/**
 */
static void dev_canceled(struct arsdk_peer *peer,
		const struct arsdk_peer_info *info,
		enum arsdk_conn_cancel_reason reason,
		void *userdata)
{
	struct test_dev *dev = userdata;
	dev->peer = NULL;
}

/**
 */
static void dev_link_status(struct arsdk_peer *peer,
		const struct arsdk_peer_info *info,
		enum arsdk_link_status status,
		void *userdata)
{
	if (status == ARSDK_LINK_STATUS_KO)
		arsdk_peer_disconnect(peer);
}

/**
 */
static void conn_req(struct arsdk_peer *peer,
		const struct arsdk_peer_info *info,
		void *userdata)
{
	int res = 0;
	struct test_dev *dev = userdata;
	struct arsdk_peer_conn_cfg cfg;
	struct arsdk_peer_conn_cbs cbs;
	static const char json[] = "{"
		"\"arstream_fragment_size\": 65000, "
		"\"arstream_fragment_maximum_number\": 4, "
		"\"c2d_update_port\": 51 ,"
		"\"c2d_user_port\": 21"
		"}";

	/* Only one peer at a time */
	if (dev->peer != NULL) {
		res = arsdk_peer_reject(peer);
		CU_ASSERT_EQUAL(res, 0);
		return;
	}

	/* Save peer */
	dev->peer = peer;

	memset(&cfg, 0, sizeof(cfg));
	cfg.json = json;

	memset(&cbs, 0, sizeof(cbs));
	cbs.userdata = dev;
	cbs.connected = &dev_connected;
	cbs.disconnected = &dev_disconnected;
	cbs.canceled = &dev_canceled;
	cbs.link_status = &dev_link_status;

	/* Accept connection */
	res = arsdk_peer_accept(peer, &cfg, &cbs, dev->loop);
	CU_ASSERT_EQUAL(res, 0);
}

/**
 */
static void backend_dev_create(struct test_dev *dev)
{
	int res = 0;
	struct arsdk_backend_net_cfg backend_net_cfg;
	struct arsdk_publisher_net_cfg publisher_net_cfg;
	struct arsdk_publisher_cfg publisher_cfg = {
		.name = DEVICE_NAME,
		.type = ARSDK_DEVICE_TYPE_BEBOP_2,
		.id = "12345678",
	};
	struct arsdk_backend_listen_cbs listen_cbs = {
		.userdata = dev,
		.conn_req = &conn_req,
	};
	uint16_t net_listen_port = 44444;

	memset(&backend_net_cfg, 0, sizeof(backend_net_cfg));
	res = arsdk_backend_net_new(dev->mngr, &backend_net_cfg,
			&dev->backend_net);
	CU_ASSERT_EQUAL(res, 0);
	res = arsdk_backend_net_start_listen(dev->backend_net,
			&listen_cbs, net_listen_port);
	CU_ASSERT_EQUAL(res, 0);

	/* start net publisher */
	memset(&publisher_net_cfg, 0, sizeof(publisher_net_cfg));
	publisher_net_cfg.base = publisher_cfg;
	publisher_net_cfg.port = net_listen_port;

	res = arsdk_publisher_net_new(dev->backend_net, dev->loop, NULL,
			&dev->publisher_net);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_publisher_net_start(dev->publisher_net,
			&publisher_net_cfg);
	CU_ASSERT_EQUAL(res, 0);
}

/**
 */
static void backend_dev_destroy(struct test_dev *dev)
{
	int res = 0;

	res = arsdk_publisher_net_stop(dev->publisher_net);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_publisher_net_destroy(dev->publisher_net);
	CU_ASSERT_EQUAL(res, 0);
	dev->publisher_net = NULL;

	if (dev->peer != NULL) {
		res = arsdk_peer_disconnect(dev->peer);
		CU_ASSERT_EQUAL(res, 0);
		dev->peer = NULL;
	}

	res = arsdk_backend_net_stop_listen(dev->backend_net);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_backend_net_destroy(dev->backend_net);
	CU_ASSERT_EQUAL(res, 0);

	dev->backend_net = NULL;
}

/**
 */
void test_create_dev(struct pomp_loop *loop, struct test_dev **dev)
{
	int res = 0;

	s_dev.loop = loop;

	/* Create manager */
	res = arsdk_mngr_new(s_dev.loop, &s_dev.mngr);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_NOT_EQUAL(s_dev.mngr, NULL);

	/* Create backend */
	backend_dev_create(&s_dev);

	*dev = &s_dev;
}

/**
 */
void test_delete_dev(struct test_dev *dev)
{
	backend_dev_destroy(dev);
	arsdk_mngr_destroy(dev->mngr);
}
