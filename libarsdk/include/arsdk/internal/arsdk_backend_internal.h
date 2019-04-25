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

#ifndef _ARSDK_BACKEND_INTERNAL_H_
#define _ARSDK_BACKEND_INTERNAL_H_

/** */
struct arsdk_peer_conn_internal_cbs {
	void  *userdata;

	void (*connected)(struct arsdk_peer *peer,
			struct arsdk_peer_conn *conn,
			struct arsdk_transport *transport,
			void *userdata);

	void (*disconnected)(struct arsdk_peer *peer,
			struct arsdk_peer_conn *conn,
			void *userdata);

	void (*canceled)(struct arsdk_peer *peer,
			struct arsdk_peer_conn *conn,
			enum arsdk_conn_cancel_reason reason,
			void *userdata);
};

/** */
struct arsdk_backend_ops {

	int (*accept_peer_conn)(struct arsdk_backend *base,
			struct arsdk_peer *peer,
			struct arsdk_peer_conn *conn,
			const struct arsdk_peer_conn_cfg *cfg,
			const struct arsdk_peer_conn_internal_cbs *cbs,
			struct pomp_loop *loop);

	int (*reject_peer_conn)(struct arsdk_backend *base,
			struct arsdk_peer *peer,
			struct arsdk_peer_conn *conn);

	int (*stop_peer_conn)(struct arsdk_backend *base,
			struct arsdk_peer *peer,
			struct arsdk_peer_conn *conn);

	void (*socket_cb)(struct arsdk_backend *base,
			int fd, enum arsdk_socket_kind kind);
};

ARSDK_API int arsdk_backend_new(void *child, struct arsdk_mngr *mngr,
		const char *name, enum arsdk_backend_type type,
		const struct arsdk_backend_ops *ops,
		struct arsdk_backend **ret_obj);

ARSDK_API int arsdk_backend_destroy(struct arsdk_backend *self);

ARSDK_API enum arsdk_backend_type arsdk_backend_get_type(
		struct arsdk_backend *self);

ARSDK_API const char *arsdk_backend_get_name(struct arsdk_backend *self);

ARSDK_API void *arsdk_backend_get_child(struct arsdk_backend *self);

ARSDK_API int arsdk_backend_set_osdata(struct arsdk_backend *self,
		void *osdata);

ARSDK_API void *arsdk_backend_get_osdata(struct arsdk_backend *self);

ARSDK_API int arsdk_backend_create_peer(
		struct arsdk_backend *self,
		const struct arsdk_peer_info *info,
		struct arsdk_peer_conn *conn,
		struct arsdk_peer **ret_obj);

ARSDK_API int arsdk_backend_destroy_peer(
		struct arsdk_backend *self,
		struct arsdk_peer *peer);

ARSDK_API int arsdk_backend_accept_peer_conn(
		struct arsdk_backend *self,
		struct arsdk_peer *peer,
		struct arsdk_peer_conn *conn,
		const struct arsdk_peer_conn_cfg *cfg,
		const struct arsdk_peer_conn_internal_cbs *cbs,
		struct pomp_loop *loop);

ARSDK_API int arsdk_backend_reject_peer_conn(
		struct arsdk_backend *self,
		struct arsdk_peer *peer,
		struct arsdk_peer_conn *conn);

ARSDK_API int arsdk_backend_stop_peer_conn(
		struct arsdk_backend *self,
		struct arsdk_peer *peer,
		struct arsdk_peer_conn *conn);

ARSDK_API int arsdk_backend_socket_cb(
		struct arsdk_backend *self,
		int fd, enum arsdk_socket_kind kind);

#endif /* !_ARSDK_BACKEND_INTERNAL_H_ */
