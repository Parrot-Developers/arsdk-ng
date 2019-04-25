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

/** */
struct test_data {
	int connected;
	int stopped;
	struct pomp_loop *loop;
	struct test_ctrl *ctrl;
	struct test_dev *dev;
	struct pomp_timer *timer;
};

/** */
static struct test_data s_data = {
	.connected = 0,
	.stopped = 0,
	.loop = NULL,
	.ctrl = NULL,
	.dev = NULL,
	.timer = NULL,
};

void test_connected(void)
{
	s_data.connected = 1;
	test_start_send_msgs(s_data.ctrl);
}

void test_end(void)
{
	s_data.stopped = 1;
}

/**
 */
static void end_timer_cb(struct pomp_timer *timer, void *userdata)
{
	if (!s_data.connected) {
		CU_FAIL("timeout");
		s_data.stopped = 1;
	}
}

/** */
static void test_protoc(void)
{
	int res = 0;

	/* Create loop */
	s_data.loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL(s_data.loop);

	/* Device part */
	test_create_dev(s_data.loop, &s_data.dev);

	/* Controller part */
	test_create_ctrl(s_data.loop, &s_data.ctrl);

	/* Create timeout cb*/
	s_data.timer = pomp_timer_new(s_data.loop, &end_timer_cb, &s_data);
	/* Setup periodic timer to send 'PCMD' commands */
	res = pomp_timer_set(s_data.timer, TEST_CONNECT_TIMEOUT);
	CU_ASSERT_EQUAL(res, 0);

	/* Run loop */
	while (!s_data.stopped)
		pomp_loop_wait_and_process(s_data.loop, -1);

	res = pomp_timer_clear(s_data.timer);
	CU_ASSERT_EQUAL(res, 0);

	res = pomp_timer_destroy(s_data.timer);
	CU_ASSERT_EQUAL(res, 0);

	/* Cleanup */
	test_delete_ctrl(s_data.ctrl);
	test_delete_dev(s_data.dev);

	res = pomp_loop_destroy(s_data.loop);
	CU_ASSERT_EQUAL(res, 0);
}

/* Disable some gcc warnings for test suite descriptions */
#ifdef __GNUC__
#  pragma GCC diagnostic ignored "-Wcast-qual"
#endif /* __GNUC__ */

/** */
static CU_TestInfo s_protoc_tests[] = {
	{(char *)"protoc", &test_protoc},
	CU_TEST_INFO_NULL,
};

/** */
/*extern*/ CU_SuiteInfo g_suites_protoc[] = {
	{(char *)"protoc", NULL, NULL, s_protoc_tests},
	CU_SUITE_INFO_NULL,
};
