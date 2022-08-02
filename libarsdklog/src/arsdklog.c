/**
 * Copyright (c) 2022 Parrot Drones SAS
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

#include <arsdk/security.pb-c.h>
#include <arsdklog/arsdklog.h>

#ifdef BUILD_LIBMSGHUB
#	include <msghub.h>
#endif

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>

#define ULOG_TAG arsdklog
#include <ulog.h>
#ifndef _WIN32
#	include <ulogbin.h>
#endif
ULOG_DECLARE_TAG(ULOG_TAG);

#define ARSDKLOG_VERSION 3

#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)
#define ARSDKLOG_TAG "arsdk-" TO_STRING(ARSDKLOG_VERSION)
#define ARSDKLOG_TAG_LEN sizeof(ARSDKLOG_TAG)

/* Previous versions had a bug where the tag was "arsdk-ARSDK_LOG_VERSION"
 * instead of the expected "arsdk-3", so we need to handle this tag as a v3 tag
 */
#define ERR_ARSDKLOG_TAG "arsdk-ARSDK_LOG_VERSION"
#define ERR_ARSDKLOG_TAG_LEN sizeof(ERR_ARSDKLOG_TAG)


#ifndef SIZEOF_ARRAY
#	define SIZEOF_ARRAY(x) (sizeof((x)) / sizeof((x)[0]))
#endif


/**
 * Binary header for all v3 logs, followed by the event payload.
 */
struct arsdklog_v3_header {
	uint32_t event;
	uint32_t instance_id;
	uint32_t type;
	uint32_t seq;
	uint32_t count;
	uint32_t size;
} __attribute__((packed));

struct arsdk_logger {
	int log_fd;
	unsigned int instance_id;
};

#ifdef BUILD_LIBMSGHUB
static bool is_generic_command_sensitive(const struct arsdk_cmd *cmd, bool ack)
{
	struct arsdk_binary payload = {NULL, 0};
	uint16_t service_id = 0;
	uint16_t msg_num = 0;
	int ret = 0;

	if (ack)
		ret = arsdk_cmd_dec_Generic_Custom_cmd(
			cmd, &service_id, &msg_num, &payload);
	else
		ret = arsdk_cmd_dec_Generic_Custom_cmd_non_ack(
			cmd, &service_id, &msg_num, &payload);

	if (ret < 0) {
		ULOG_ERRNO("arsdk_cmd_dec_Generic_Custom_cmd", -ret);
		return true; /* Do not log if error, in case it's sensitive */
	}

	if (service_id == msghub_utils_get_service_id(
				  arsdk__security__command__descriptor.name))
		if (msg_num == ARSDK__SECURITY__COMMAND__ID_REGISTER_APC_TOKEN)
			return true;

	return false;
}
#else
static bool is_generic_command_sensitive(const struct arsdk_cmd *cmd, bool ack)
{
	/* If we don't have access to libmsghub, consider any generic command
	 * as sensitive */
	(void)cmd;
	(void)ack;
	return true;
}
#endif /* BUILD_LIBMSGHUB */

static bool anonymize_command(const struct arsdk_cmd *in, struct arsdk_cmd *out)
{
	const char *hidden_stuff = "********";
	int ret;

	switch (in->id) {
	case ARSDK_ID_ARDRONE3_NETWORKSETTINGS_WIFISECURITY: {
		/* Decode command and replace key */
		int32_t type = 0;
		const char *key = NULL;
		int32_t key_type = 0;
		ret = arsdk_cmd_dec_Ardrone3_NetworkSettings_WifiSecurity(
			in, &type, &key, &key_type);
		if (ret < 0)
			goto error_copy;
		arsdk_cmd_enc_Ardrone3_NetworkSettings_WifiSecurity(
			out, type, hidden_stuff, key_type);
		break;
	}

	case ARSDK_ID_ARDRONE3_NETWORKSETTINGSSTATE_WIFISECURITY: {
		/* Decode command and replace key */
		int32_t type = 0;
		const char *key = NULL;
		int32_t key_type = 0;
		ret = arsdk_cmd_dec_Ardrone3_NetworkSettingsState_WifiSecurity(
			in, &type, &key, &key_type);
		if (ret < 0)
			goto error_copy;
		arsdk_cmd_enc_Ardrone3_NetworkSettingsState_WifiSecurity(
			out, type, hidden_stuff, key_type);
		break;
	}

	case ARSDK_ID_WIFI_SET_SECURITY: {
		/* Decode command and replace key */
		int32_t type = 0;
		const char *key = NULL;
		int32_t key_type = 0;
		ret = arsdk_cmd_dec_Wifi_Set_security(
			in, &type, &key, &key_type);
		if (ret < 0)
			goto error_copy;
		arsdk_cmd_enc_Wifi_Set_security(
			out, type, hidden_stuff, key_type);
		break;
	}

	case ARSDK_ID_WIFI_SECURITY_CHANGED: {
		/* Decode command and replace key */
		int32_t type = 0;
		const char *key = NULL;
		int32_t key_type = 0;
		ret = arsdk_cmd_dec_Wifi_Security_changed(
			in, &type, &key, &key_type);
		if (ret < 0)
			goto error_copy;
		arsdk_cmd_enc_Wifi_Security_changed(
			out, type, hidden_stuff, key_type);
		break;
	}

	case ARSDK_ID_USER_STORAGE_ENCRYPTION_PASSWORD: {
		/* Decode command and replace password */
		const char *password;
		int32_t type;
		ret = arsdk_cmd_dec_User_storage_Encryption_password(
			in, &password, &type);
		if (ret < 0)
			goto error_copy;
		arsdk_cmd_enc_User_storage_Encryption_password(
			out, hidden_stuff, type);
		break;
	}

	case ARSDK_ID_USER_STORAGE_V2_ENCRYPTION_PASSWORD: {
		/* Decode command and replace password */
		uint8_t storage_id;
		int32_t type;
		const char *password;
		ret = arsdk_cmd_dec_User_storage_v2_Encryption_password(
			in, &storage_id, &password, &type);
		if (ret < 0)
			goto error_copy;
		arsdk_cmd_enc_User_storage_v2_Encryption_password(
			out, storage_id, hidden_stuff, type);
		break;
	}

	/* TODO: Anonymize generic commands instead of filter */
	case ARSDK_ID_GENERIC_CUSTOM_CMD:
		if (is_generic_command_sensitive(in, true))
			goto skip_log;
		arsdk_cmd_copy(out, in);
		break;

	case ARSDK_ID_GENERIC_CUSTOM_CMD_NON_ACK:
		if (is_generic_command_sensitive(in, false))
			goto skip_log;
		arsdk_cmd_copy(out, in);
		break;

	default:
		/* Nothing to do (copy with new ref input) */
		arsdk_cmd_copy(out, in);
		break;
	}

	return true;

error_copy:
	ULOGW("Unable to anonymize command %u.%u.%u",
	      in->prj_id,
	      in->cls_id,
	      in->cmd_id);
	arsdk_cmd_copy(out, in);
	return true;

skip_log:
	ULOGD("Command %u.%u.%u not logged",
	      in->prj_id,
	      in->cls_id,
	      in->cmd_id);
	return false;
}


int arsdk_logger_create(const char *ulog_device,
			unsigned int instance_id,
			struct arsdk_logger **ret_logger)
{
#ifdef _WIN32
	return -ENOSYS;
#else
	ULOG_ERRNO_RETURN_ERR_IF(!ret_logger, EINVAL);

	struct arsdk_logger *logger = calloc(1, sizeof(*logger));

	logger->instance_id = instance_id;

	logger->log_fd = ulog_bin_open(ulog_device);
	if (logger->log_fd < 0) {
		int ret = logger->log_fd;
		free(logger);
		ULOG_ERRNO("ulog_bin_open", -ret);
		return ret;
	}
	*ret_logger = logger;
	return 0;
#endif
}


int arsdk_logger_destroy(struct arsdk_logger *logger)
{
#ifdef _WIN32
	return -ENOSYS;
#else
	if (logger)
		ulog_bin_close(logger->log_fd);
	free(logger);
	return 0;
#endif
}


int arsdk_logger_log_cmd(struct arsdk_logger *logger,
			 enum arsdk_cmd_dir dir,
			 const struct arsdk_cmd *cmd)
{
#ifdef _WIN32
	return -ENOSYS;
#else
	struct iovec iov[2];
	struct arsdk_cmd anonymized;
	const struct arsdk_cmd_desc *cmd_desc = NULL;
	const void *cdata;
	size_t len;
	int ret = 0;

	ULOG_ERRNO_RETURN_ERR_IF(!logger, EINVAL);
	ULOG_ERRNO_RETURN_ERR_IF(!cmd, EINVAL);

	struct arsdklog_v3_header header = {
		.instance_id = logger->instance_id,
		.type = cmd->buffer_type,
	};
	if (header.type == ARSDK_CMD_BUFFER_TYPE_INVALID) {
		/* Find the type from the command description */
		cmd_desc = arsdk_cmd_find_desc(cmd);
		if (cmd_desc != NULL)
			header.type = cmd_desc->buffer_type;
	}

	switch (dir) {
	case ARSDK_CMD_DIR_TX:
		header.event = ARSDKLOG_EVENT_CMD_PUSHED;
		break;
	case ARSDK_CMD_DIR_RX:
		header.event = ARSDKLOG_EVENT_CMD_POPPED;
		break;
	default:
		ULOGE("Unknown direction (%d)", dir);
		return -EINVAL;
	}


	arsdk_cmd_init(&anonymized);
	if (!anonymize_command(cmd, &anonymized))
		goto skip_log;

	pomp_buffer_get_cdata(anonymized.buf, &cdata, &len, NULL);
	iov[0].iov_base = &header;
	iov[0].iov_len = sizeof(header);
	iov[1].iov_base = (void *)cdata;
	iov[1].iov_len = len;
	ret = ulog_bin_writev_chunk(logger->log_fd,
				    ARSDKLOG_TAG,
				    ARSDKLOG_TAG_LEN,
				    NULL,
				    0,
				    iov,
				    SIZEOF_ARRAY(iov));

skip_log:
	arsdk_cmd_clear(&anonymized);

	return ret;
#endif
}


int arsdk_logger_log_cmd_send_status(struct arsdk_logger *logger,
				     const struct arsdk_cmd *cmd,
				     enum arsdk_cmd_buffer_type type,
				     enum arsdk_cmd_itf_cmd_send_status status,
				     uint16_t seq)
{
#ifdef _WIN32
	return -ENOSYS;
#else
	struct iovec iov[2];
	struct arsdk_cmd anonymized;
	const void *cdata;
	size_t len;
	int ret = 0;

	ULOG_ERRNO_RETURN_ERR_IF(!logger, EINVAL);
	ULOG_ERRNO_RETURN_ERR_IF(!cmd, EINVAL);

	struct arsdklog_v3_header header = {
		.instance_id = logger->instance_id,
		.type = type,
		.seq = seq,
	};

	switch (status) {
	case ARSDK_CMD_ITF_CMD_SEND_STATUS_PARTIALLY_PACKED:
		header.count = 1;
		/* fallthrough */
	case ARSDK_CMD_ITF_CMD_SEND_STATUS_PACKED:
		header.event = ARSDKLOG_EVENT_CMD_PACKED;
		break;
	case ARSDK_CMD_ITF_CMD_SEND_STATUS_ACK_RECEIVED:
		header.event = ARSDKLOG_EVENT_CMD_ACK;
		break;
	case ARSDK_CMD_ITF_CMD_SEND_STATUS_TIMEOUT:
		header.count = 1;
		/* fallthrough */
	case ARSDK_CMD_ITF_CMD_SEND_STATUS_CANCELED:
		header.event = ARSDKLOG_EVENT_CMD_ABORTED;
		break;
	default:
		ULOGE("Unknown status (%d)", status);
		return -EINVAL;
	}

	arsdk_cmd_init(&anonymized);
	if (!anonymize_command(cmd, &anonymized))
		goto skip_log;

	pomp_buffer_get_cdata(anonymized.buf, &cdata, &len, NULL);
	iov[0].iov_base = &header;
	iov[0].iov_len = sizeof(header);
	iov[1].iov_base = (void *)cdata;
	iov[1].iov_len = len;
	ret = ulog_bin_writev_chunk(logger->log_fd,
				    ARSDKLOG_TAG,
				    ARSDKLOG_TAG_LEN,
				    NULL,
				    0,
				    iov,
				    SIZEOF_ARRAY(iov));

skip_log:
	arsdk_cmd_clear(&anonymized);

	return ret;
#endif
}


int arsdk_logger_log_pack_send_status(
	struct arsdk_logger *logger,
	int seq,
	enum arsdk_cmd_buffer_type type,
	size_t len,
	enum arsdk_cmd_itf_pack_send_status status,
	uint32_t count)
{
#ifdef _WIN32
	return -ENOSYS;
#else
	struct iovec iov[1];

	ULOG_ERRNO_RETURN_ERR_IF(!logger, EINVAL);

	struct arsdklog_v3_header header = {
		.instance_id = logger->instance_id,
		.type = type,
		.seq = seq,
		.count = count,
		.size = len,
	};

	switch (status) {
	case ARSDK_CMD_ITF_PACK_SEND_STATUS_SENT:
		header.event = ARSDKLOG_EVENT_PACK_SENT;
		break;
	case ARSDK_CMD_ITF_PACK_SEND_STATUS_ACK_RECEIVED:
		header.event = ARSDKLOG_EVENT_PACK_ACK_RECV;
		break;
	case ARSDK_CMD_ITF_PACK_SEND_STATUS_TIMEOUT:
		header.count = 1;
		header.event = ARSDKLOG_EVENT_PACK_ABORTED;
		break;
	case ARSDK_CMD_ITF_PACK_SEND_STATUS_CANCELED:
		header.count = 0;
		header.event = ARSDKLOG_EVENT_PACK_ABORTED;
		break;
	default:
		ULOGE("Unknown status (%d)", status);
		return -EINVAL;
	}

	iov[0].iov_base = &header;
	iov[0].iov_len = sizeof(header);
	return ulog_bin_writev_chunk(logger->log_fd,
				     ARSDKLOG_TAG,
				     ARSDKLOG_TAG_LEN,
				     NULL,
				     0,
				     iov,
				     SIZEOF_ARRAY(iov));
#endif
}


int arsdk_logger_log_pack_recv_status(
	struct arsdk_logger *logger,
	int seq,
	enum arsdk_cmd_buffer_type type,
	size_t len,
	enum arsdk_cmd_itf_pack_recv_status status)
{
#ifdef _WIN32
	return -ENOSYS;
#else
	struct iovec iov[1];

	ULOG_ERRNO_RETURN_ERR_IF(!logger, EINVAL);

	struct arsdklog_v3_header header = {
		.instance_id = logger->instance_id,
		.type = type,
		.seq = seq,
		.size = len,
	};

	switch (status) {
	case ARSDK_CMD_ITF_PACK_RECV_STATUS_IGNORED:
		header.count = 1;
		/* fallthrough */
	case ARSDK_CMD_ITF_PACK_RECV_STATUS_PROCESSED:
		header.event = ARSDKLOG_EVENT_PACK_RECV;
		break;
	case ARSDK_CMD_ITF_PACK_RECV_STATUS_ACK_SENT:
		header.event = ARSDKLOG_EVENT_PACK_ACK_SENT;
		break;
	default:
		ULOGE("Unknown status (%d)", status);
		return -EINVAL;
	}

	iov[0].iov_base = &header;
	iov[0].iov_len = sizeof(header);
	return ulog_bin_writev_chunk(logger->log_fd,
				     ARSDKLOG_TAG,
				     ARSDKLOG_TAG_LEN,
				     NULL,
				     0,
				     iov,
				     SIZEOF_ARRAY(iov));
#endif
}


int arsdk_logger_log_event_parse(const char *tag,
				 const char *payload,
				 size_t payload_size,
				 struct arsdklog_evt_info *info)
{
	int ver, ret;
	unsigned int id;
	char kind[16] = {0};
	size_t i;

	ULOG_ERRNO_RETURN_ERR_IF(!tag, EINVAL);
	ULOG_ERRNO_RETURN_ERR_IF(!payload, EINVAL);
	ULOG_ERRNO_RETURN_ERR_IF(!info, EINVAL);

	memset(info, 0, sizeof(*info));

	/* Search for the v3 tag */
	if (strcmp(tag, ARSDKLOG_TAG) == 0 ||
	    strcmp(tag, ERR_ARSDKLOG_TAG) == 0) {
		struct arsdklog_v3_header header;
		if (payload_size < sizeof(header) + 1)
			return -EINVAL;
		memcpy(&info->chunk_id, payload, 1);
		if (info->chunk_id > 0) {
			/* For continuation chunks, we don't have the header */
			info->payload = payload + 1;
			info->payload_size = payload_size - 1;
			return 0;
		}
		memcpy(&header, payload + 1, sizeof(header));
		info->event = header.event;
		info->instance_id = header.instance_id;
		info->type = header.type;
		info->seq = header.seq;
		info->count = header.count;
		info->size = header.size;
		info->payload = payload + sizeof(header) + 1;
		info->payload_size = payload_size - sizeof(header) - 1;
		return 0;
	}

	/* Fallback for a v1 tag */
	ret = sscanf(tag, "arsdk-%u-%u-%15s", &ver, &id, kind);
	if (ret != 3)
		return -EINVAL;
	if (ver != 1)
		return -EINVAL;

	info->instance_id = id;
	info->payload = payload;
	info->payload_size = payload_size;

	const struct {
		const char *tag;
		enum arsdklog_event evt;
	} v1_events[] = {
		{"pushed", ARSDKLOG_EVENT_CMD_PUSHED},
		{"popped", ARSDKLOG_EVENT_CMD_POPPED},
	};

	for (i = 0; i < SIZEOF_ARRAY(v1_events); i++) {
		if (strcmp(kind, v1_events[i].tag) == 0) {
			info->event = v1_events[i].evt;
			break;
		}
	}
	if (info->event == ARSDKLOG_EVENT_INVALID)
		return -EINVAL;
	return 0;
}
