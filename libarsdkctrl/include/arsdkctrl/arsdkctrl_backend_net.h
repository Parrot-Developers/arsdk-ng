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

#ifndef _ARSDKCTRL_BACKEND_NET_H_
#define _ARSDKCTRL_BACKEND_NET_H_

/** */
struct arsdkctrl_backend_net;

/** */
struct arsdkctrl_backend_net_cfg {
	const char        *iface;
	int               qos_mode_supported;
	/* stream internal support:
	 * - Set to 1 to internally handle default streaming channel (port
	 *   allocation and exchange in json)
	 * - Set to 0 to handle it externally (no port allocation nor automatic
	 *   exchange in json, it is up to application to do it) */
	int               stream_supported;
};

/** */
struct arsdk_device_net_cfg {
	const char              *name;
	enum arsdk_device_type  type;
	const char              *addr;
	uint16_t                port;
	const char              *id;
	const char              *discovery;
};

/**
 * Context socket callback. If set, will be called after socket is created.
 * @param self : backend net.
 * @param fd : socket fd.
 * @param type : transport type on the socket.
 * @param userdata : user data given in arsdk_mngr_set_socket_cb.
 */
typedef void (*arsdkctrl_backend_net_socket_cb_t)(
		struct arsdkctrl_backend_net *self,
		int fd,
		enum arsdk_socket_kind kind,
		void *userdata);

ARSDK_API int arsdkctrl_backend_net_new(struct arsdk_ctrl *ctrl,
		const struct arsdkctrl_backend_net_cfg *cfg,
		struct arsdkctrl_backend_net **ret_obj);

ARSDK_API int arsdkctrl_backend_net_destroy(
		struct arsdkctrl_backend_net *self);

ARSDK_API struct arsdkctrl_backend *arsdkctrl_backend_net_get_parent(
		struct arsdkctrl_backend_net *self);

/**
 * Set the function to call when socket fd are created. This allows application
 * to configure socket before it is used.
 * @param self : backend net.
 * @param cb : function to call when socket are created.
 * @param userdata : User data given in callback.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdkctrl_backend_net_set_socket_cb(
		struct arsdkctrl_backend_net *self,
		arsdkctrl_backend_net_socket_cb_t cb, void *userdata);

#endif /* _ARSDKCTRL_BACKEND_NET_H_ */
