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

#ifndef _ARSDK_FTP_CMD_H_
#define _ARSDK_FTP_CMD_H_

struct arsdk_ftp_cmd;

struct arsdk_ftp_cmd_result {
	int                     code;
	union {
		int             data_stream_port;
		size_t          file_size;
	} param;
};

enum arsdk_ftp_cmd_type {
	ARSDK_FTP_CMD_TYPE_USER = 0,
	ARSDK_FTP_CMD_TYPE_PASS,
	ARSDK_FTP_CMD_TYPE_PWD,
	ARSDK_FTP_CMD_TYPE_CWD,
	ARSDK_FTP_CMD_TYPE_TYPE,
	ARSDK_FTP_CMD_TYPE_LIST,
	ARSDK_FTP_CMD_TYPE_SIZE,
	ARSDK_FTP_CMD_TYPE_RETR,
	ARSDK_FTP_CMD_TYPE_STOR,
	ARSDK_FTP_CMD_TYPE_EPSV, /*FTP Extensions for IPv6 and NATs*/
	ARSDK_FTP_CMD_TYPE_DELE,
	ARSDK_FTP_CMD_TYPE_RMD,
	ARSDK_FTP_CMD_TYPE_COUNT,
	ARSDK_FTP_CMD_TYPE_RNFR,
	ARSDK_FTP_CMD_TYPE_RNTO,
	ARSDK_FTP_CMD_TYPE_REST,
	ARSDK_FTP_CMD_TYPE_APPE,
};

enum arsdk_ftp_cmd_data_type {
	ARSDK_FTP_CMD_DATA_TYPE_NONE = 0,
	ARSDK_FTP_CMD_DATA_TYPE_IN,
	ARSDK_FTP_CMD_DATA_TYPE_OUT,
};

struct arsdk_ftp_cmd_desc {
	enum arsdk_ftp_cmd_type         cmd_type;
	const char                      *code;
	const int                       resp_code;
	enum arsdk_ftp_cmd_data_type    data_type;
};

/* commands descriptions */
static const struct arsdk_ftp_cmd_desc ARSDK_FTP_CMD_USER = {
	.cmd_type = ARSDK_FTP_CMD_TYPE_USER,
	.code = "USER",
	.resp_code = 230,
	.data_type = ARSDK_FTP_CMD_DATA_TYPE_NONE,
};

static const struct arsdk_ftp_cmd_desc ARSDK_FTP_CMD_PASS = {
	.cmd_type = ARSDK_FTP_CMD_TYPE_PASS,
	.code = "PASS",
	.resp_code = 230,
	.data_type = ARSDK_FTP_CMD_DATA_TYPE_NONE,
};

static const struct arsdk_ftp_cmd_desc ARSDK_FTP_CMD_CWD = {
	.cmd_type = ARSDK_FTP_CMD_TYPE_CWD,
	.code = "CWD",
	.resp_code = 250,
	.data_type = ARSDK_FTP_CMD_DATA_TYPE_NONE,
};

static const struct arsdk_ftp_cmd_desc ARSDK_FTP_CMD_RNFR = {
	.cmd_type = ARSDK_FTP_CMD_TYPE_RNFR,
	.code = "RNFR",
	.resp_code = 350,
	.data_type = ARSDK_FTP_CMD_DATA_TYPE_NONE,
};

static const struct arsdk_ftp_cmd_desc ARSDK_FTP_CMD_RNTO = {
	.cmd_type = ARSDK_FTP_CMD_TYPE_RNTO,
	.code = "RNTO",
	.resp_code = 250,
	.data_type = ARSDK_FTP_CMD_DATA_TYPE_NONE,
};

static const struct arsdk_ftp_cmd_desc ARSDK_FTP_CMD_DELE = {
	.cmd_type = ARSDK_FTP_CMD_TYPE_DELE,
	.code = "DELE",
	.resp_code = 250,
	.data_type = ARSDK_FTP_CMD_DATA_TYPE_NONE,
};

static const struct arsdk_ftp_cmd_desc ARSDK_FTP_CMD_RMD = {
	.cmd_type = ARSDK_FTP_CMD_TYPE_RMD,
	.code = "RMD",
	.resp_code = 250,
	.data_type = ARSDK_FTP_CMD_DATA_TYPE_NONE,
};

static const struct arsdk_ftp_cmd_desc ARSDK_FTP_CMD_EPSV = {
	.cmd_type = ARSDK_FTP_CMD_TYPE_EPSV,
	.code = "EPSV",
	.resp_code = 229,
	.data_type = ARSDK_FTP_CMD_DATA_TYPE_NONE,
};

static const struct arsdk_ftp_cmd_desc ARSDK_FTP_CMD_TYPE = {
	.cmd_type = ARSDK_FTP_CMD_TYPE_TYPE,
	.code = "TYPE",
	.resp_code = 200,
	.data_type = ARSDK_FTP_CMD_DATA_TYPE_NONE,
};

static const struct arsdk_ftp_cmd_desc ARSDK_FTP_CMD_LIST = {
	.cmd_type = ARSDK_FTP_CMD_TYPE_LIST,
	.code = "LIST",
	.resp_code = 150,
	.data_type = ARSDK_FTP_CMD_DATA_TYPE_IN,
};

static const struct arsdk_ftp_cmd_desc ARSDK_FTP_CMD_SIZE = {
	.cmd_type = ARSDK_FTP_CMD_TYPE_SIZE,
	.code = "SIZE",
	.resp_code = 213,
	.data_type = ARSDK_FTP_CMD_DATA_TYPE_NONE,
};

static const struct arsdk_ftp_cmd_desc ARSDK_FTP_CMD_RETR = {
	.cmd_type = ARSDK_FTP_CMD_TYPE_RETR,
	.code = "RETR",
	.resp_code = 150,
	.data_type = ARSDK_FTP_CMD_DATA_TYPE_IN,
};

static const struct arsdk_ftp_cmd_desc ARSDK_FTP_CMD_STOR = {
	.cmd_type = ARSDK_FTP_CMD_TYPE_STOR,
	.code = "STOR",
	.resp_code = 150,
	.data_type = ARSDK_FTP_CMD_DATA_TYPE_OUT,
};

static const struct arsdk_ftp_cmd_desc ARSDK_FTP_CMD_REST = {
	.cmd_type = ARSDK_FTP_CMD_TYPE_REST,
	.code = "REST",
	.resp_code = 350,
	.data_type = ARSDK_FTP_CMD_DATA_TYPE_NONE,
};

static const struct arsdk_ftp_cmd_desc ARSDK_FTP_CMD_APPE = {
	.cmd_type = ARSDK_FTP_CMD_TYPE_APPE,
	.code = "APPE",
	.resp_code = 150,
	.data_type = ARSDK_FTP_CMD_DATA_TYPE_OUT,
};


int arsdk_ftp_cmd_enc(const struct arsdk_ftp_cmd_desc *desc, char *param,
		struct pomp_buffer **ret_buff);

static inline
int arsdk_ftp_cmd_enc_user(char *username, struct pomp_buffer **ret_buff)
{
	return arsdk_ftp_cmd_enc(&ARSDK_FTP_CMD_USER, username, ret_buff);
};

static inline
int arsdk_ftp_cmd_enc_pass(char *pass, struct pomp_buffer **ret_buff)
{
	return arsdk_ftp_cmd_enc(&ARSDK_FTP_CMD_PASS, pass, ret_buff);
};

int arsdk_ftp_cmd_dec(struct pomp_buffer *buff,
		struct arsdk_ftp_cmd_result *result);

#endif /* !_ARSDK_FTP_CMD_H_ */
