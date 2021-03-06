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

#ifndef _ARSDK_DISCOVERY_NET_H_
#define _ARSDK_DISCOVERY_NET_H_

struct arsdk_discovery_net;

ARSDK_API int arsdk_discovery_net_new(struct arsdk_ctrl *ctrl,
		struct arsdkctrl_backend_net *backend,
		const struct arsdk_discovery_cfg *cfg,
		const char *addr,
		struct arsdk_discovery_net **ret_obj);

ARSDK_API int arsdk_discovery_net_new_with_port(struct arsdk_ctrl *ctrl,
		struct arsdkctrl_backend_net *backend,
		const struct arsdk_discovery_cfg *cfg,
		const char *addr,
		uint16_t port,
		struct arsdk_discovery_net **ret_obj);

ARSDK_API int arsdk_discovery_net_destroy(struct arsdk_discovery_net *self);

ARSDK_API int arsdk_discovery_net_start(struct arsdk_discovery_net *self);

ARSDK_API int arsdk_discovery_net_stop(struct arsdk_discovery_net *self);

#endif /* _ARSDK_DISCOVERY_NET_H_ */
