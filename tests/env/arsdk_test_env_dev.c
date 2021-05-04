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

#include "arsdk_test_env_dev.h"

#define LOG_TAG "arsdk_test_env_dev"
#include "arsdk_test_log.h"

static void backend_destroy_mux(struct arsdk_test_env_dev *self);

struct arsdk_test_env_dev {
	struct pomp_loop             *loop;
	struct arsdk_mngr            *mngr;
	enum arsdk_backend_type      backend_type;
	struct {
		struct {
			struct arsdk_backend_net     *backend;
			struct arsdk_publisher_net   *publisher;
		} net;

		struct {
			struct arsdk_test_env_mux_tip *server;

			struct arsdk_publisher_cfg publisher_cfg;
			struct arsdk_backend_listen_cbs listen_cbs;

			struct arsdk_backend_mux     *backend;
			struct arsdk_publisher_mux   *publisher;
		} mux;
	} transport;
	struct arsdk_peer            *peer;
	struct arsdk_cmd_itf         *cmd_itf;

	struct arsdk_peer_conn_cbs   cbs;
};

/**
 */
static void connected(struct arsdk_peer *peer,
		const struct arsdk_peer_info *info,
		void *userdata)
{
	struct arsdk_test_env_dev *self = userdata;

	TST_LOG_FUNC();

	if (self->cbs.connected != NULL)
		(self->cbs.connected)(peer, info, self->cbs.userdata);
}

/**
 */
static void disconnected(struct arsdk_peer *peer,
		const struct arsdk_peer_info *info,
		void *userdata)
{
	struct arsdk_test_env_dev *self = userdata;

	TST_LOG_FUNC();

	if (self->cbs.disconnected != NULL)
		(self->cbs.disconnected)(peer, info, self->cbs.userdata);

	self->cmd_itf = NULL;
	self->peer = NULL;
}

/**
 */
static void canceled(struct arsdk_peer *peer,
		const struct arsdk_peer_info *info,
		enum arsdk_conn_cancel_reason reason,
		void *userdata)
{
	TST_LOG("%s: reason=%s", __func__, arsdk_conn_cancel_reason_str(reason));

	struct arsdk_test_env_dev *self = userdata;

	if (self->cbs.canceled != NULL)
		(self->cbs.canceled)(peer, info, reason, self->cbs.userdata);

	self->peer = NULL;
}

/**
 */
static void link_status(struct arsdk_peer *peer,
		const struct arsdk_peer_info *info,
		enum arsdk_link_status status,
		void *userdata)
{
	struct arsdk_test_env_dev *self = userdata;

	TST_LOG("%s: status=%s", __func__, arsdk_link_status_str(status));

	if (self->cbs.link_status != NULL)
		(self->cbs.link_status)(peer, info, status, self->cbs.userdata);

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
	struct arsdk_test_env_dev *self = userdata;
	struct arsdk_peer_conn_cfg cfg;
	struct arsdk_peer_conn_cbs cbs;
	static const char json[] = "{"
		"\"arstream_fragment_size\": 65000, "
		"\"arstream_fragment_maximum_number\": 4, "
		"\"c2d_update_port\": 51 ,"
		"\"c2d_user_port\": 21"
		"}";

	TST_LOG_FUNC();

	/* Only one peer at a time */
	if (self->peer != NULL) {
		res = arsdk_peer_reject(peer);
		CU_ASSERT_EQUAL_FATAL(res, 0);
		return;
	}

	/* Save peer */
	self->peer = peer;

	memset(&cfg, 0, sizeof(cfg));
	cfg.json = json;

	memset(&cbs, 0, sizeof(cbs));
	cbs.userdata = self;
	cbs.connected = &connected;
	cbs.disconnected = &disconnected;
	cbs.canceled = &canceled;
	cbs.link_status = &link_status;

	/* Accept connection */
	res = arsdk_peer_accept(peer, &cfg, &cbs, self->loop);
	CU_ASSERT_EQUAL_FATAL(res, 0);
}

/**
 */
static void socket_cb(struct arsdk_backend_net *self, int fd,
		enum arsdk_socket_kind kind, void *userdata)
{
	TST_LOG("socket_cb : self:%p fd:%d kind:%s userdata:%p",
			self, fd, arsdk_socket_kind_str(kind), userdata);
}

/**
 */
static void backend_create_net(struct arsdk_test_env_dev *self,
		struct arsdk_publisher_cfg *publisher_cfg,
		struct arsdk_backend_listen_cbs *listen_cbs)
{
	TST_LOG_FUNC();

	int res = 0;
	uint16_t net_listen_port = 44444;

	struct arsdk_backend_net_cfg backend_net_cfg = {};
	res = arsdk_backend_net_new(self->mngr, &backend_net_cfg,
			&self->transport.net.backend);
	CU_ASSERT_EQUAL_FATAL(res, 0);
	CU_ASSERT_PTR_NOT_NULL_FATAL(self->transport.net.backend);

	res = arsdk_backend_net_set_socket_cb(self->transport.net.backend,
			&socket_cb, self);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	res = arsdk_backend_net_start_listen(self->transport.net.backend,
			listen_cbs, net_listen_port);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	/* Start net publisher */
	struct arsdk_publisher_net_cfg publisher_net_cfg = {
		.base = *publisher_cfg,
		.port = net_listen_port,
	};

	res = arsdk_publisher_net_new(self->transport.net.backend,
			self->loop, NULL, &self->transport.net.publisher);
	CU_ASSERT_EQUAL_FATAL(res, 0);
	CU_ASSERT_PTR_NOT_NULL_FATAL(self->transport.net.backend);

	res = arsdk_publisher_net_start(self->transport.net.publisher,
			&publisher_net_cfg);
	CU_ASSERT_EQUAL_FATAL(res, 0);
}

static void backend_destroy_net(struct arsdk_test_env_dev *self)
{
	TST_LOG_FUNC();

	int res;
	if (self->transport.net.publisher != NULL) {
		res = arsdk_publisher_net_stop(self->transport.net.publisher);
		CU_ASSERT_EQUAL_FATAL(res, 0);

		arsdk_publisher_net_destroy(self->transport.net.publisher);
		self->transport.net.publisher = NULL;
	}

	if (self->transport.net.backend != NULL) {
		res = arsdk_backend_net_stop_listen(self->transport.net.backend);
		CU_ASSERT_EQUAL_FATAL(res, 0);

		res = arsdk_backend_net_destroy(self->transport.net.backend);
		CU_ASSERT_EQUAL_FATAL(res, 0);

		self->transport.net.backend = NULL;
	}
}

static void on_mux_connect(struct arsdk_test_env_mux_tip *mux_tip, void *userdata)
{
	TST_LOG_FUNC();

	struct arsdk_test_env_dev *self = userdata;
	CU_ASSERT_PTR_NOT_NULL_FATAL(self);

	struct mux_ctx *mctx = arsdk_test_env_mux_tip_get_mctx(
			self->transport.mux.server);
	CU_ASSERT_PTR_NOT_NULL_FATAL(mctx);

	/* Create mux backend */
	struct arsdk_backend_mux_cfg backend_mux_cfg = {
		.mux = mctx,
	};
	int res = arsdk_backend_mux_new(self->mngr, &backend_mux_cfg,
			&self->transport.mux.backend);
	CU_ASSERT_EQUAL_FATAL(res, 0);
	CU_ASSERT_PTR_NOT_NULL_FATAL(self->transport.mux.backend);

	res = arsdk_backend_mux_start_listen(self->transport.mux.backend,
			&self->transport.mux.listen_cbs);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	/* Create mux publisher. */
	res = arsdk_publisher_mux_new(self->transport.mux.backend,
			mctx, &self->transport.mux.publisher);
	CU_ASSERT_EQUAL_FATAL(res, 0);
	CU_ASSERT_PTR_NOT_NULL_FATAL(self->transport.mux.publisher);

	res = arsdk_publisher_mux_start(self->transport.mux.publisher,
			&self->transport.mux.publisher_cfg);
	CU_ASSERT_EQUAL_FATAL(res, 0);
}

static void on_mux_disconnect(struct arsdk_test_env_mux_tip *mux_tip, void *userdata)
{
	TST_LOG_FUNC();

}

/**
 */
static void backend_create_mux(struct arsdk_test_env_dev *self,
		struct arsdk_publisher_cfg *publisher_cfg,
		struct arsdk_backend_listen_cbs *listen_cbs)
{
	TST_LOG_FUNC();

	self->transport.mux.publisher_cfg = *publisher_cfg;
	self->transport.mux.listen_cbs = *listen_cbs;

	struct arsdk_test_env_mux_tip_cbs tip_cbs = {
		.on_connect = &on_mux_connect,
		.on_disconnect = &on_mux_disconnect,
		.userdata = self,
	};

	/* Create mux server. */
	int res = arsdk_test_env_mux_tip_new(self->loop,
			ARSDK_TEST_ENV_MUX_TIP_TYPE_SERVER, &tip_cbs,
			&self->transport.mux.server);
	CU_ASSERT_EQUAL_FATAL(res, 0);
	CU_ASSERT_PTR_NOT_NULL_FATAL(self->transport.mux.server);

	res = arsdk_test_env_mux_tip_start(self->transport.mux.server);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	/* Wait mux connection ... */
}

static void backend_destroy_mux(struct arsdk_test_env_dev *self)
{
	TST_LOG_FUNC();

	int res;
	if (self->transport.mux.publisher != NULL) {
		res = arsdk_publisher_mux_stop(self->transport.mux.publisher);
		CU_ASSERT_EQUAL_FATAL(res, 0);

		arsdk_publisher_mux_destroy(self->transport.mux.publisher);
		self->transport.mux.publisher = NULL;
	}

	if (self->transport.mux.backend != NULL) {
		res = arsdk_backend_mux_stop_listen(self->transport.mux.backend);
		CU_ASSERT_EQUAL_FATAL(res, 0);

		res = arsdk_backend_mux_destroy(self->transport.mux.backend);
		CU_ASSERT_EQUAL_FATAL(res, 0);

		self->transport.mux.backend = NULL;
	}

	if (self->transport.mux.server != NULL) {
		res = arsdk_test_env_mux_tip_stop(self->transport.mux.server);
		CU_ASSERT_EQUAL_FATAL(res, 0);

		arsdk_test_env_mux_tip_destroy(self->transport.mux.server);
		self->transport.mux.server = NULL;
	}
}


/**
 */
static void backend_create(struct arsdk_test_env_dev *self)
{
	TST_LOG_FUNC();

	struct arsdk_publisher_cfg publisher_cfg = {
		.name = "Device",
		.type = ARSDK_DEVICE_TYPE_ANAFI_2,
		.id = "12345678",
	};

	struct arsdk_backend_listen_cbs listen_cbs = {
		.userdata = self,
		.conn_req = &conn_req,
	};

	switch (self->backend_type) {
	case ARSDK_BACKEND_TYPE_NET:
		backend_create_net(self, &publisher_cfg, &listen_cbs);

		break;
	case ARSDK_BACKEND_TYPE_MUX:
		backend_create_mux(self, &publisher_cfg, &listen_cbs);
		break;

	default:
		CU_FAIL_FATAL("Unsupported backend");
		return;
	}

	/* wait for connection request */
}

static void backend_destroy(struct arsdk_test_env_dev *self)
{
	TST_LOG_FUNC();

	backend_destroy_net(self);

	backend_destroy_mux(self);
}

int arsdk_test_env_dev_new(struct pomp_loop *loop,
		enum arsdk_backend_type backend_type,
		struct arsdk_peer_conn_cbs *cbs,
		struct arsdk_test_env_dev **ret_device)
{
	TST_LOG_FUNC();

	struct arsdk_test_env_dev *self = NULL;

	self = calloc(1, sizeof(*self));
	CU_ASSERT_PTR_NOT_NULL_FATAL(self);

	self->backend_type = backend_type;
	self->loop = loop;
	self->cbs = *cbs;

	/* Create manager */
	int res = arsdk_mngr_new(loop, &self->mngr);
	CU_ASSERT_PTR_NOT_NULL_FATAL(self->mngr);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	backend_create(self);

	*ret_device = self;
	return 0;
}

int arsdk_test_env_dev_destroy(struct arsdk_test_env_dev *self)
{
	TST_LOG_FUNC();

	CU_ASSERT_PTR_NOT_NULL_FATAL(self);

	backend_destroy(self);

	int res = arsdk_mngr_destroy(self->mngr);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	free(self);
	return 0;
}