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

#ifndef _ARSDK_TRANSPORT_NET_H_
#define _ARSDK_TRANSPORT_NET_H_

/** */
struct arsdk_transport_net;

/** */
struct arsdk_transport_net_cfg {
	/** protocol version to used */
	uint32_t   proto_v;
	in_addr_t  tx_addr;
	int        qos_mode;
	int        stream_supported;

	struct {
		uint16_t rx_port;
		uint16_t tx_port;
	} data;
};

/** */
struct arsdk_transport_net_cbs {
	void *userdata;

	void (*socketcb)(struct arsdk_transport_net *self,
			int fd,
			enum arsdk_socket_kind kind,
			void *userdata);
};

ARSDK_API int arsdk_transport_net_new(struct pomp_loop *loop,
		const struct arsdk_transport_net_cfg *cfg,
		const struct arsdk_transport_net_cbs *cbs,
		struct arsdk_transport_net **ret_obj);

ARSDK_API struct arsdk_transport *arsdk_transport_net_get_parent(
		struct arsdk_transport_net *self);

/** Get current configuration once rx port values are known and bound */
ARSDK_API int arsdk_transport_net_get_cfg(struct arsdk_transport_net *self,
		struct arsdk_transport_net_cfg *cfg);

/** Update configuration once tx port values are known */
ARSDK_API int arsdk_transport_net_update_cfg(struct arsdk_transport_net *self,
		const struct arsdk_transport_net_cfg *cfg);

ARSDK_API int arsdk_transport_net_socket_cb(struct arsdk_transport_net *self,
		int fd,
		enum arsdk_socket_kind kind);

#endif /* !_ARSDK_TRANSPORT_NET_H_ */
