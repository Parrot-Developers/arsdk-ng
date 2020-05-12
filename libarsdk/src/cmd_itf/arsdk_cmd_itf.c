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
#include "arsdk_cmd_itf_priv.h"
#include "arsdk_cmd_itf1.h"
#include "arsdk_cmd_itf2.h"
#include "arsdk_default_log.h"

/**
 */
const char *arsdk_cmd_itf_send_status_str(enum arsdk_cmd_itf_send_status val)
{
	switch (val) {
	case ARSDK_CMD_ITF_SEND_STATUS_SENT:
		return "SENT";
	case ARSDK_CMD_ITF_SEND_STATUS_ACK_RECEIVED:
		return "ACK_RECEIVED";
	case ARSDK_CMD_ITF_SEND_STATUS_TIMEOUT:
		return "TIMEOUT";
	case ARSDK_CMD_ITF_SEND_STATUS_CANCELED:
		return "CANCELED";
	default:
		return "UNKNOWN";
	}
}

static void itf1_dispose(struct arsdk_cmd_itf1 *itf1, void *userdata)
{
	struct arsdk_cmd_itf *self = userdata;
	ARSDK_RETURN_IF_FAILED(self != NULL, -EINVAL);

	/* Notify internal callbacks */
	(*self->internal_cbs.dispose)(self, self->internal_cbs.userdata);

	/* Notify command interface callback */
	if (self->cbs.dispose)
		(*self->cbs.dispose)(self, self->cbs.userdata);
}

static void itf2_dispose(struct arsdk_cmd_itf2 *itf2, void *userdata)
{
	struct arsdk_cmd_itf *self = userdata;
	ARSDK_RETURN_IF_FAILED(self != NULL, -EINVAL);

	/* Notify internal callbacks */
	(*self->internal_cbs.dispose)(self, self->internal_cbs.userdata);

	/* Notify command interface callback */
	if (self->cbs.dispose)
		(*self->cbs.dispose)(self, self->cbs.userdata);
}

/**
 */
int arsdk_cmd_itf_new(struct arsdk_transport *transport,
		const struct arsdk_cmd_itf_cbs *cbs,
		const struct arsdk_cmd_itf_internal_cbs *internal_cbs,
		const struct arsdk_cmd_queue_info *tx_info_table,
		uint32_t tx_count,
		uint8_t ackoff,
		struct arsdk_cmd_itf **ret_itf)
{
	int res;
	struct arsdk_cmd_itf *self = NULL;
	struct arsdk_cmd_itf1_cbs itf1_cbs = {
		.dispose = &itf1_dispose,
	};
	struct arsdk_cmd_itf2_cbs itf2_cbs = {
		.dispose = &itf2_dispose,
	};

	ARSDK_RETURN_ERR_IF_FAILED(ret_itf != NULL, -EINVAL);
	*ret_itf = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(transport != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->recv_cmd != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(internal_cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(internal_cbs->dispose != NULL, -EINVAL);

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Initialize structure */
	self->cbs = *cbs;
	self->internal_cbs = *internal_cbs;
	self->proto_v = arsdk_transport_get_proto_v(transport);
	if (self->proto_v > 1) {
		itf2_cbs.userdata = self;
		res = arsdk_cmd_itf2_new(transport, &itf2_cbs, cbs, self,
				tx_info_table, tx_count, ackoff,
				&self->core.v2);
	} else {
		itf1_cbs.userdata = self;
		res = arsdk_cmd_itf1_new(transport, &itf1_cbs, cbs, self,
				tx_info_table, tx_count, ackoff,
				&self->core.v1);
	}
	if (res < 0)
		goto error;

	*ret_itf = self;
	return 0;
error:
	arsdk_cmd_itf_destroy(self);
	return res;
}

/**
 */
int arsdk_cmd_itf_destroy(struct arsdk_cmd_itf *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	if (self->proto_v > 1)
		arsdk_cmd_itf2_destroy(self->core.v2);
	else
		arsdk_cmd_itf1_destroy(self->core.v1);


	free(self);
	return 0;
}

/**
 */
int arsdk_cmd_itf_set_osdata(struct arsdk_cmd_itf *itf, void *userdata)
{
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);
	itf->osdata = userdata;
	return 0;
}

/**
 */
void *arsdk_cmd_itf_get_osdata(struct arsdk_cmd_itf *itf)
{
	return itf == NULL ? NULL : itf->osdata;
}

/**
 */
int arsdk_cmd_itf_stop(struct arsdk_cmd_itf *self)
{
	int res;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	if (self->proto_v > 1)
		res = arsdk_cmd_itf2_stop(self->core.v2);
	else
		res = arsdk_cmd_itf1_stop(self->core.v1);

	return res;
}

/**
 */
int arsdk_cmd_itf_send(struct arsdk_cmd_itf *self,
		const struct arsdk_cmd *cmd,
		arsdk_cmd_itf_send_status_cb_t send_status,
		void *userdata)
{
	int res;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	if (self->proto_v > 1) {
		res = arsdk_cmd_itf2_send(self->core.v2, cmd, send_status,
				userdata);
	} else {
		res = arsdk_cmd_itf1_send(self->core.v1, cmd, send_status,
				userdata);
	}

	return res;
}

/**
 */
int arsdk_cmd_itf_recv_data(struct arsdk_cmd_itf *self,
		const struct arsdk_transport_header *header,
		const struct arsdk_transport_payload *payload)
{
	int res;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	if (self->proto_v > 1)
		res = arsdk_cmd_itf2_recv_data(self->core.v2, header, payload);
	else
		res = arsdk_cmd_itf1_recv_data(self->core.v1, header, payload);

	return res;
}

const char *arsdk_arg_type_str(enum arsdk_arg_type val)
{
	switch (val) {
	case ARSDK_ARG_TYPE_I8: return "i8";
	case ARSDK_ARG_TYPE_U8: return "u8";
	case ARSDK_ARG_TYPE_I16: return "i16";
	case ARSDK_ARG_TYPE_U16: return "u16";
	case ARSDK_ARG_TYPE_I32: return "i32";
	case ARSDK_ARG_TYPE_U32: return "u32";
	case ARSDK_ARG_TYPE_I64: return "i64";
	case ARSDK_ARG_TYPE_U64: return "u64";
	case ARSDK_ARG_TYPE_FLOAT: return "float";
	case ARSDK_ARG_TYPE_DOUBLE: return "double";
	case ARSDK_ARG_TYPE_STRING: return "string";
	case ARSDK_ARG_TYPE_ENUM: return "enum";
	default: return "unknown";
	}
}
