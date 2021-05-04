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

#ifndef _ARSDK_TRANSPORT_MUX_H_
#define _ARSDK_TRANSPORT_MUX_H_

/** */
struct arsdk_transport_mux;

struct arsdk_transport_mux_cfg {
	/** protocol version to used */
	uint32_t   proto_v;
	int        stream_supported;
};

ARSDK_API int arsdk_transport_mux_new(
		struct mux_ctx *mux,
		struct pomp_loop *loop,
		const struct arsdk_transport_mux_cfg *cfg,
		struct arsdk_transport_mux **ret_obj);

ARSDK_API struct arsdk_transport *arsdk_transport_mux_get_parent(
		struct arsdk_transport_mux *self);

ARSDK_API int arsdk_transport_mux_get_cfg(struct arsdk_transport_mux *self,
		struct arsdk_transport_mux_cfg *cfg);

ARSDK_API int arsdk_transport_mux_update_cfg(struct arsdk_transport_mux *self,
		const struct arsdk_transport_mux_cfg *cfg);

#endif /* _ARSDK_TRANSPORT_MUX_H_ */
