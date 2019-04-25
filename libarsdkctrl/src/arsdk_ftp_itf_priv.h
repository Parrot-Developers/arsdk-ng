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

#ifndef _ARSDK_FTP_ITF_PRIV_H_
#define _ARSDK_FTP_ITF_PRIV_H_

/** */
struct arsdk_ftp_itf_internal_cbs {
	void *userdata;

	int (*dispose)(struct arsdk_ftp_itf *itf,
			void *userdata);

	void (*socketcb)(struct arsdk_ftp_itf *itf,
			int fd,
			enum arsdk_socket_kind kind,
			void *userdata);
};

/**
 */
int arsdk_ftp_itf_new(struct arsdk_transport *transport,
		const struct arsdk_ftp_itf_internal_cbs *internal_cbs,
		const struct arsdk_device_info *dev_info,
		struct mux_ctx *mux,
		struct arsdk_ftp_itf **ret_itf);

/**
 */
int arsdk_ftp_itf_destroy(struct arsdk_ftp_itf *itf);

/**
 */
int arsdk_ftp_itf_stop(struct arsdk_ftp_itf *itf);

/**
 */
int arsdk_ftp_file_new(struct arsdk_ftp_file **ret_file);

/**
 */
void arsdk_ftp_file_destroy(struct arsdk_ftp_file *file);

/**
 */
int arsdk_ftp_file_set_name(struct arsdk_ftp_file *file,
		const char *name);

#endif /* !_ARSDK_FTP_ITF_PRIV_H_ */
