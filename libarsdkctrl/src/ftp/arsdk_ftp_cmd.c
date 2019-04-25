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

#include "arsdkctrl_priv.h"
#include "arsdk_ftp_log.h"
#include "arsdk_ftp_cmd.h"

#define ARSDK_FTP_CMD_FOOT_LEN 2

int arsdk_ftp_cmd_enc(const struct arsdk_ftp_cmd_desc *desc, char *param,
		struct pomp_buffer **ret_buff)
{
	char cmd_str[500];
	struct pomp_buffer *buff = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(desc != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_buff != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(param != NULL, -EINVAL);

	snprintf(cmd_str, sizeof(cmd_str), "%s %s\r\n",
			desc->code, (char *)param);

	buff = pomp_buffer_new_with_data(cmd_str, strlen(cmd_str));
	if (buff == NULL)
		return -ENOMEM;

	*ret_buff = buff;
	return 0;
}

static int parse_229_param(struct arsdk_ftp_cmd_result *response, char *param)
{
	int res = 0;

	ARSDK_RETURN_ERR_IF_FAILED(response != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(param != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(response->code == 229, -EINVAL);

	/* Open data stream */
	res = sscanf(param, "%*[^|]|||%d|", &response->param.data_stream_port);
	if (res != 1)
		return -EPROTO;

	return 0;
}

static int parse_213_param(struct arsdk_ftp_cmd_result *response, char *param)
{
	int res = 0;

	ARSDK_RETURN_ERR_IF_FAILED(response != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(param != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(response->code == 213, -EINVAL);

	/* Open data stream */
	res = sscanf(param, "%zu", &response->param.file_size);
	if (res != 1)
		return -EPROTO;

	return 0;
}

static int parse_param(struct arsdk_ftp_cmd_result *response, char *param)
{
	ARSDK_RETURN_ERR_IF_FAILED(response != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(param != NULL, -EINVAL);

	switch (response->code) {
	case 229:
		return parse_229_param(response, param);
	case 213:
		return parse_213_param(response, param);
	default:
		return 0;
	}
}

int arsdk_ftp_cmd_dec(struct pomp_buffer *buff,
		struct arsdk_ftp_cmd_result *result)
{
	int res = 0;
	size_t len = 0;
	const void *cdata = NULL;
	char *str = NULL;
	char *param = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(buff != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(result != NULL, -EINVAL);

	/* initialize the response */
	memset(result, 0, sizeof(*result));

	res = pomp_buffer_get_cdata(buff, &cdata, &len, NULL);
	if (res < 0)
		return res;

	/* check response length */
	if (len < ARSDK_FTP_CMD_FOOT_LEN)
		return -EPROTO;

	str = strndup((const char *)cdata, len - ARSDK_FTP_CMD_FOOT_LEN);
	ARSDK_LOGI("< %s", str);
	res = sscanf(str, "%d", &result->code);
	if (res < 0)
		goto end;

	param = strchr(str, ' ');
	if (param != NULL) {
		param++;
		res = parse_param(result, param);
		if (res < 0)
			goto end;
	}

end:
	free(str);
	return res;
}
