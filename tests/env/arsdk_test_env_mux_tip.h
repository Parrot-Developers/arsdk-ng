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

#ifndef _ARSDK_TEST_ENV_MUX_TIP_H_
#define _ARSDK_TEST_ENV_MUX_TIP_H_

#include "libmux.h"

/* forward declaration. */
struct arsdk_test_env_mux_tip;

enum arsdk_test_env_mux_tip_type {
	ARSDK_TEST_ENV_MUX_TIP_TYPE_CLIENT,
	ARSDK_TEST_ENV_MUX_TIP_TYPE_SERVER,
};

struct arsdk_test_env_mux_tip_cbs {
	void (*on_connect)(struct arsdk_test_env_mux_tip *self, void *userdata);
	void (*on_disconnect)(struct arsdk_test_env_mux_tip *self, void *userdata);
	void *userdata;
};

int arsdk_test_env_mux_tip_new(struct pomp_loop *loop,
		enum arsdk_test_env_mux_tip_type type,
		struct arsdk_test_env_mux_tip_cbs *cbs,
		struct arsdk_test_env_mux_tip **ret_tip);

void arsdk_test_env_mux_tip_destroy(struct arsdk_test_env_mux_tip *self);

int arsdk_test_env_mux_tip_start(struct arsdk_test_env_mux_tip *self);

int arsdk_test_env_mux_tip_stop(struct arsdk_test_env_mux_tip *self);

struct mux_ctx *arsdk_test_env_mux_tip_get_mctx(struct arsdk_test_env_mux_tip *self);

#endif /* !_ARSDK_TEST_ENV_MUX_TIP_H_ */