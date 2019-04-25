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

#ifndef _ARSDK_FTP_SEQ_H_
#define _ARSDK_FTP_SEQ_H_

struct arsdk_ftp_seq;

/** */
enum arsdk_ftp_seq_status {
	ARSDK_FTP_SEQ_STATUS_OK = 0,
	ARSDK_FTP_SEQ_CANCELED,
	ARSDK_FTP_SEQ_FAILED,
	ARSDK_FTP_SEQ_ABORTED,
};

struct arsdk_ftp_seq_cbs {
	void *userdata;

	void (*complete)(struct arsdk_ftp_seq *seq,
			enum arsdk_ftp_seq_status status,
			int error,
			void *userdata);

	int (*data_recv)(struct arsdk_ftp_seq *seq,
			struct pomp_buffer *buff,
			void *userdata);

	size_t (*data_send)(struct arsdk_ftp_seq *seq, void *buffer,
			size_t cap, void *userdata);

	void (*file_size)(struct arsdk_ftp_seq *seq,
			size_t size,
			void *userdata);

	void (*socketcb)(struct arsdk_ftp_seq *seq,
			int fd,
			void *userdata);
};

int arsdk_ftp_seq_new(struct pomp_loop *loop,
		struct arsdk_ftp_conn *conn,
		const struct arsdk_ftp_seq_cbs *cbs,
		struct arsdk_ftp_seq **ret_seq);

int arsdk_ftp_seq_destroy(struct arsdk_ftp_seq *seq);

int arsdk_ftp_seq_append(struct arsdk_ftp_seq *seq,
		const struct arsdk_ftp_cmd_desc *desc, const char *param);

static inline int arsdk_ftp_seq_append_uint64(struct arsdk_ftp_seq *seq,
		const struct arsdk_ftp_cmd_desc *desc, const uint64_t param)
{
	char param_str[21];
	snprintf(param_str, sizeof(param_str), "%"PRIu64, param);
	return arsdk_ftp_seq_append(seq, desc, param_str);
};

int arsdk_ftp_seq_start(struct arsdk_ftp_seq *seq);

int arsdk_ftp_seq_stop(struct arsdk_ftp_seq *seq);

#endif /* !_ARSDK_FTP_SEQ_H_ */
