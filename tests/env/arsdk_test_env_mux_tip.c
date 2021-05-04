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

#include "libpomp.h"
#include <errno.h>
#include <netinet/in.h>

#include "libmux.h"
#include "arsdk_test_env_mux_tip.h"

#define LOG_TAG "arsdk_test_env_mux_tip"
#include "arsdk_test_log.h"

#define TST_LOG_FUNC_TYPE(type) TST_LOG("%s (%s)", __func__, (type) == 0 ? "CLIENT" : "SERVER")

struct arsdk_test_env_mux_tip {
	enum arsdk_test_env_mux_tip_type type;
	struct pomp_loop *loop;
	struct pomp_ctx *pctx;
	struct mux_ctx *mctx;

	struct pomp_conn *conn;
	struct arsdk_test_env_mux_tip_cbs cbs;
};

static void tip_raw_cb(struct pomp_ctx *ctx,
		struct pomp_conn *conn,
		struct pomp_buffer *buf,
		void *userdata)
{
	struct arsdk_test_env_mux_tip *self = userdata;
	CU_ASSERT_PTR_NOT_NULL_FATAL(self);

	//TST_LOG_FUNC_TYPE(self->type);

	/* Decode read data, rx operation or channel queues will handle
	 * decoded data */
	mux_decode(self->mctx, buf);
}

static int mux_tx_cb(struct mux_ctx *ctx, struct pomp_buffer *buf,
		void *userdata)
{
	struct arsdk_test_env_mux_tip *self = userdata;
	CU_ASSERT_PTR_NOT_NULL_FATAL(self);

	//TST_LOG_FUNC_TYPE(self->type);

	return pomp_ctx_send_raw_buf(self->pctx, buf);
}

static void mux_rx_cb(struct mux_ctx *ctx, uint32_t chanid,
		enum mux_channel_event event, struct pomp_buffer *buf,
		void *userdata)
{
	size_t len = 0;

	struct arsdk_test_env_mux_tip *self = userdata;
	CU_ASSERT_PTR_NOT_NULL_FATAL(self);
	TST_LOG_FUNC_TYPE(self->type);

	pomp_buffer_get_cdata(buf, NULL, &len, NULL);
}

static void mux_release_cb(struct mux_ctx *ctx, void *userdata)
{
	struct arsdk_test_env_mux_tip *self = userdata;
	CU_ASSERT_PTR_NOT_NULL_FATAL(self);
	TST_LOG_FUNC_TYPE(self->type);
}

static void tip_event_cb(struct pomp_ctx *ctx,
		enum pomp_event event,
		struct pomp_conn *conn,
		const struct pomp_msg *msg,
		void *userdata)
{
	int res;
	struct arsdk_test_env_mux_tip *self = userdata;
	struct mux_ops ops = {
		.tx = &mux_tx_cb,
		.chan_cb = &mux_rx_cb,
		.release = &mux_release_cb,
	};

	CU_ASSERT_PTR_NOT_NULL_FATAL(self);

	TST_LOG_FUNC_TYPE(self->type);

	switch (event) {
	case POMP_EVENT_CONNECTED:
		CU_ASSERT_PTR_NULL(self->conn);

		ops.userdata = self;
		self->mctx = mux_new(-1, self->loop, &ops, 0);
		CU_ASSERT_PTR_NOT_NULL_FATAL(self->mctx);

		self->conn = conn;
		(*self->cbs.on_connect)(self, self->cbs.userdata);
		break;

	case POMP_EVENT_DISCONNECTED:
		CU_ASSERT_EQUAL(self->conn, conn);

		if (self->mctx != NULL) {
			res = mux_stop(self->mctx);
			CU_ASSERT_EQUAL(res, 0);
			mux_unref(self->mctx);
			self->mctx = NULL;
		}

		(*self->cbs.on_disconnect)(self, self->cbs.userdata);
		break;

	case POMP_EVENT_MSG:
		/* Never received for raw context */
		break;
	}
}

int arsdk_test_env_mux_tip_new(struct pomp_loop *loop,
		enum arsdk_test_env_mux_tip_type type,
		struct arsdk_test_env_mux_tip_cbs *cbs,
		struct arsdk_test_env_mux_tip **ret_tip)
{
	struct arsdk_test_env_mux_tip *self;

	TST_LOG_FUNC_TYPE(type);

	if (loop == NULL ||
	    cbs == NULL ||
	    cbs->on_connect == NULL ||
	    cbs->on_disconnect == NULL)
		return -ENOMEM;

	self = calloc(1, sizeof(*self));
	CU_ASSERT_PTR_NULL_FATAL(self->conn);

	self->pctx = pomp_ctx_new_with_loop(&tip_event_cb, self, loop);
	pomp_ctx_set_raw(self->pctx, &tip_raw_cb);

	self->type = type;
	self->loop = loop;
	self->cbs = *cbs;

	*ret_tip = self;
	return 0;
}

void arsdk_test_env_mux_tip_destroy(struct arsdk_test_env_mux_tip *self)
{
	CU_ASSERT_PTR_NOT_NULL_FATAL(self);

	TST_LOG_FUNC_TYPE(self->type);
	pomp_ctx_destroy(self->pctx);
	free(self);
}

int arsdk_test_env_mux_tip_start(struct arsdk_test_env_mux_tip *self)
{
	struct sockaddr_in addr;
	socklen_t addrlen = 0;

	CU_ASSERT_PTR_NOT_NULL_FATAL(self);

	TST_LOG_FUNC_TYPE(self->type);

	/* Setup address */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(4321);
	addrlen = sizeof(addr);

	switch (self->type) {
	case ARSDK_TEST_ENV_MUX_TIP_TYPE_CLIENT:
		pomp_ctx_connect(self->pctx,
			(const struct sockaddr *)&addr, addrlen);
		break;
	case ARSDK_TEST_ENV_MUX_TIP_TYPE_SERVER:
		pomp_ctx_listen(self->pctx,
			(const struct sockaddr *)&addr, addrlen);
		break;
	default:
		break;
	}

	return 0;
}

int arsdk_test_env_mux_tip_stop(struct arsdk_test_env_mux_tip *self)
{
	int res;

	CU_ASSERT_PTR_NOT_NULL_FATAL(self);

	TST_LOG_FUNC_TYPE(self->type);

	if (self->mctx != NULL) {
		res = mux_stop(self->mctx);
		CU_ASSERT_EQUAL(res, 0);
		mux_unref(self->mctx);
		self->mctx = NULL;
	}

	pomp_ctx_stop(self->pctx);

	return 0;
}

struct mux_ctx *arsdk_test_env_mux_tip_get_mctx(
		struct arsdk_test_env_mux_tip *self)
{
	return (self != NULL) ? self->mctx : NULL;
}