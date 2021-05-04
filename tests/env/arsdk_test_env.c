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

#include "arsdk_test_env_dev.h"
#include "arsdk_test_env_ctrl.h"

#include "arsdk_test_env.h"

#define LOG_TAG "arsdk_test_env"
#include "arsdk_test_log.h"

struct arsdk_test_env {
	struct arsdk_test_env_cbs cbs;
	struct pomp_loop *loop;
	int running;
	enum arsdk_backend_type backend_type;

	struct arsdk_test_env_dev *dev;
	struct arsdk_test_env_ctrl *ctrl;
};

int arsdk_test_env_new(enum arsdk_backend_type backend_type,
		struct arsdk_test_env_cbs *cbs,
		struct arsdk_test_env **ret_env)
{
	TST_LOG_FUNC();

	struct arsdk_test_env *env;

	CU_ASSERT_PTR_NOT_NULL_FATAL(cbs);
	CU_ASSERT_PTR_NOT_NULL_FATAL(ret_env);

	env = calloc(1, sizeof(*env));
	CU_ASSERT_PTR_NOT_NULL_FATAL(env);

	env->backend_type = backend_type;
	env->cbs = *cbs;
	env->loop = pomp_loop_new();
	CU_ASSERT_PTR_NOT_NULL_FATAL(env->loop);

	*ret_env = env;
	return 0;
}

int arsdk_test_env_destroy(struct arsdk_test_env *env)
{
	CU_ASSERT_PTR_NOT_NULL_FATAL(env);

	TST_LOG_FUNC();

	pomp_loop_destroy(env->loop);

	free(env);
	return 0;
}

int arsdk_test_env_start(struct arsdk_test_env *env)
{
	TST_LOG_FUNC();

	int res = arsdk_test_env_dev_new(env->loop, env->backend_type,
			&env->cbs.device_cbs,&env->dev);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	res = arsdk_test_env_ctrl_new(env->loop, env->backend_type,
			&env->cbs.ctrl_cbs, &env->ctrl);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	return 0;
}

int arsdk_test_env_stop(struct arsdk_test_env *env)
{
	TST_LOG_FUNC();

	int res;

	if (env->ctrl != NULL) {
		res = arsdk_test_env_ctrl_destroy(env->ctrl);
		CU_ASSERT_EQUAL_FATAL(res, 0);

		env->ctrl = NULL;
	}

	if (env->dev != NULL) {
		res = arsdk_test_env_dev_destroy(env->dev);
		CU_ASSERT_EQUAL_FATAL(res, 0);

		env->dev = NULL;
	}

	return 0;
}

int arsdk_test_env_run_loop(struct arsdk_test_env *env)
{
	CU_ASSERT_PTR_NOT_NULL_FATAL(env);

	TST_LOG_FUNC();

	env->running = 1;
	while (env->running) {
		pomp_loop_wait_and_process(env->loop, -1);
	}

	return 0;
}

void arsdk_test_env_loop_stop(struct arsdk_test_env *env)
{
	TST_LOG_FUNC();

	CU_ASSERT_PTR_NOT_NULL_FATAL(env);
	env->running = 0;
}