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

#include "arsdk_priv.h"

#ifndef _ARSDK_FTP_CONN_H_
#define _ARSDK_FTP_CONN_H_

struct arsdk_ftp_conn;

struct arsdk_ftp_conn_cbs {
	void *userdata;

	void (*connected)(struct arsdk_ftp_conn *comm, void *userdata);

	void (*disconnected)(struct arsdk_ftp_conn *comm, void *userdata);

	void (*recv_response)(struct arsdk_ftp_conn *comm,
			struct arsdk_ftp_cmd_result *response, void *userdata);

	void (*socketcb)(struct arsdk_ftp_conn *comm,
			int fd,
			void *userdata);
};

int arsdk_ftp_conn_new(struct pomp_loop *loop,
		const struct sockaddr *addr,
		uint32_t addrlen,
		const char *username,
		const char *password,
		struct arsdk_ftp_conn **ret_conn);

int arsdk_ftp_conn_destroy(struct arsdk_ftp_conn *conn);

int arsdk_ftp_conn_send(struct arsdk_ftp_conn *conn, struct pomp_buffer *buff);

int arsdk_ftp_conn_is_connected(struct arsdk_ftp_conn *conn);

const struct sockaddr *arsdk_ftp_conn_get_addr(struct arsdk_ftp_conn *conn,
		uint32_t *addrlen);

int arsdk_ftp_conn_add_listener(struct arsdk_ftp_conn *conn,
		const struct arsdk_ftp_conn_cbs *cbs);

int arsdk_ftp_conn_remove_listener(struct arsdk_ftp_conn *conn,
		const void *userdata);

#endif /* !_ARSDK_FTP_CONN_H_ */
