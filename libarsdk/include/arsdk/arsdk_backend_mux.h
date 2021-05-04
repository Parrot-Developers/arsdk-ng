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

#ifndef _ARSDK_BACKEND_MUX_H_
#define _ARSDK_BACKEND_MUX_H_

/** */
struct mux_ctx;
struct arsdk_backend_mux;

/** minimum protocol version implemented */
#define ARSDK_BACKEND_MUX_PROTO_MIN ARSDK_PROTOCOL_VERSION_1
/** maximum protocol version implemented */
#define ARSDK_BACKEND_MUX_PROTO_MAX ARSDK_PROTOCOL_VERSION_3

/** */
struct arsdk_backend_mux_cfg {
	struct mux_ctx *mux;
	int stream_supported;
	/** minimum protocol version supported */
	uint32_t                               proto_v_min;
	/** maximum protocol version supported */
	uint32_t                               proto_v_max;
	/** protocol version used */
	uint32_t                               proto_v;
};

ARSDK_API int arsdk_backend_mux_new(struct arsdk_mngr *mngr,
		const struct arsdk_backend_mux_cfg *cfg,
		struct arsdk_backend_mux **ret_obj);

ARSDK_API int arsdk_backend_mux_destroy(struct arsdk_backend_mux *self);

ARSDK_API struct arsdk_backend *
arsdk_backend_mux_get_parent(struct arsdk_backend_mux *self);

ARSDK_API struct mux_ctx *arsdk_backend_mux_get_mux_ctx(
		struct arsdk_backend_mux *self);

ARSDK_API int arsdk_backend_mux_start_listen(
		struct arsdk_backend_mux *self,
		const struct arsdk_backend_listen_cbs *cbs);

ARSDK_API int arsdk_backend_mux_stop_listen(
		struct arsdk_backend_mux *self);

#endif /* _ARSDK_BACKEND_MUX_H_ */
