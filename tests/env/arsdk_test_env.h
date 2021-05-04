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

#ifndef _ARSDK_TEST_ENV_H_
#define _ARSDK_TEST_ENV_H_

/* forward declarations */
struct arsdk_test_env;

struct arsdk_test_env_cbs {
	void *userdata;

	struct arsdk_peer_conn_cbs device_cbs;
	struct arsdk_device_conn_cbs ctrl_cbs;
};

int arsdk_test_env_new(enum arsdk_backend_type backend_type,
		struct arsdk_test_env_cbs *cbs,
		struct arsdk_test_env **ret_env);

int arsdk_test_env_destroy(struct arsdk_test_env *env);

int arsdk_test_env_start(struct arsdk_test_env *env);

int arsdk_test_env_stop(struct arsdk_test_env *env);

int arsdk_test_env_run_loop(struct arsdk_test_env *env);

void arsdk_test_env_loop_stop(struct arsdk_test_env *env);

#endif /* !_ARSDK_TEST_ENV_H_ */