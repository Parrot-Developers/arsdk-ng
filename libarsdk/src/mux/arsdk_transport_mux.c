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
#include "arsdk_mux_log.h"

#ifdef BUILD_LIBMUX

#include "arsdk_mux.h"

#define ARSDK_FRAME_V1_HEADER_SIZE      7
#define ARSDK_FRAME_V2_HEADER_SIZE_MIN  6
#define ARSDK_FRAME_V2_HEADER_SIZE_MAX  14
#define ARSDK_TRANSPORT_PING_PERIOD  1000
#define ARSDK_TRANSPORT_TAG          "mux"

/** */
struct arsdk_transport_mux {
	struct arsdk_transport  *parent;
	struct arsdk_transport_mux_cfg cfg;
	struct mux_ctx          *mux;
	struct pomp_loop        *loop;
	int                     started;
};

/**
 * Reads protocol version from data.
 *
 * @param src : Source where read.
 * @param src_len : Source length.
 * @param proto_v[out] : Protocol version read.
 * @param proto_v_len[out] : Length in byte read from the source.
 *
 * @return 0 in case of success, negative errno value in case of error.
 */
static int read_proto_v(const uint8_t *src, size_t src_len,
		uint32_t *proto_v, size_t *proto_v_len)
{
	int res = futils_varint_read_u32(src, src_len, proto_v, proto_v_len);
	if (res < 0)
		return res;

	/* If version is less than the offset, it is the protocol version 1
	   and there is no protocol version data. */
	if (*proto_v < ARSDK_TRANSPORT_DATA_TYPE_MAX) {
		*proto_v = ARSDK_PROTOCOL_VERSION_1;
		*proto_v_len = 0;
		return 0;
	}

	/* Subtract protocol version offset */
	*proto_v -= ARSDK_TRANSPORT_DATA_TYPE_MAX;
	return 0;
}

/**
 * Writes protocol version in data.
 *
 * @param dst : Destination where write.
 * @param dst_len : Destination length ; should be greater or equal to 5.
 * @param proto_v : Protocol version to write.
 *        Should be less than "UINT32_MAX - ARSDK_TRANSPORT_DATA_TYPE_MAX".
 * @param proto_v_len[out] : Length in byte written in the destination.
 *
 * @return 0 in case of success, negative errno value in case of error.
 */
static int write_proto_v(uint8_t *dst, size_t dst_len,
		uint32_t proto_v, size_t *proto_v_len)
{
	if (proto_v > UINT32_MAX - ARSDK_TRANSPORT_DATA_TYPE_MAX)
		return -EINVAL;

	/* Protocol version with offset */
	return futils_varint_write_u32(dst, dst_len,
			proto_v + ARSDK_TRANSPORT_DATA_TYPE_MAX,
			proto_v_len);
}

/**
 * Decodes protocol v1 header
 *
 * @param headerbuf : Data to read.
 * @param header[out] : Header to fill with data.
 * @param payload_len[out] : Payload length read from data.
 *
 * @return 0 in case of success, negative errno value in case of error.
 * @see ARSDK_PROTOCOL_VERSION_1
 */
static int decode_header_v1(const uint8_t *headerbuf,
		struct arsdk_transport_header *header, uint32_t *payload_len)
{
	uint32_t frame_len;

	header->type = headerbuf[0];
	header->id = headerbuf[1];
	/* Sequence number in 8 bits */
	header->seq = headerbuf[2];
	/* Frame size in 32 bits */
	frame_len = headerbuf[3] |
		   (headerbuf[4] << 8) |
		   (headerbuf[5] << 16) |
		   (headerbuf[6] << 24);
	if (frame_len < ARSDK_FRAME_V1_HEADER_SIZE)
		return -EPROTO;

	*payload_len = frame_len - ARSDK_FRAME_V1_HEADER_SIZE;
	return 0;
}

/**
 * Decodes protocol v2 header
 *
 * @param buf : Data to read.
 * @param len : Data size.
 * @param header[out] : Header to fill with data.
 * @param header_len[out] : Header length in the data buffer.
 * @param payload_len[out] : Payload length read from data.
 *
 * @return 0 in case of success, negative errno value in case of error.
 * @see ARSDK_PROTOCOL_VERSION_2
 */
static int decode_header_v2(const uint8_t *buf, size_t len,
		struct arsdk_transport_header *header, size_t *header_len,
		uint32_t *payload_len)
{
	int res;
	uint32_t proto_v = 0;
	const uint8_t *data = buf;
	size_t data_len = len;
	size_t val_len = 0;

	/* Type greater than ARSDK_TRANSPORT_DATA_TYPE_MAX */
	if (len == 0 || data[0] < ARSDK_TRANSPORT_DATA_TYPE_MAX)
		return -EPROTO;

	res = read_proto_v(data, data_len, &proto_v, &val_len);
	if (res < 0)
		return -EPROTO;
	data += val_len;
	data_len -= val_len;

	/* Subtract protocol version offset */
	if (proto_v < ARSDK_PROTOCOL_VERSION_2)
		return -EPROTO;

	/* Check if there is enough data to contain the minimum data to read. */
	if (data_len < 5)
		return -EPROTO;

	header->type = data[0];
	data++;
	data_len--;

	header->id = data[0];
	data++;
	data_len--;

	/* Sequence number in 16 bits */
	header->seq = data[0] |
		     (data[1] << 8);
	data += 2;
	data_len -= 2;

	res = futils_varint_read_u32(data, data_len, payload_len, &val_len);
	if (res < 0)
		return -EPROTO;
	data += val_len;
	data_len -= val_len;

	*header_len = len - data_len;
	return 0;
}

/**
 */
static void transport_mux_rx_data(struct arsdk_transport_mux *self,
		uint32_t chanid,
		struct pomp_buffer *buf,
		void *userdata)
{
	int res;
	const void *cdata = NULL;
	const uint8_t *cdatau8 = NULL;
	size_t len = 0;
	struct arsdk_transport_header header;
	struct arsdk_transport_payload payload;
	uint32_t payloadlen = 0;
	size_t header_size = self->cfg.proto_v > ARSDK_PROTOCOL_VERSION_1 ?
					ARSDK_FRAME_V2_HEADER_SIZE_MIN :
					ARSDK_FRAME_V1_HEADER_SIZE;

	/* Get data from buffer */
	if (pomp_buffer_get_cdata(buf, &cdata, &len, NULL) < 0)
		return;

	memset(&header, 0, sizeof(header));

	switch (chanid) {
	case MUX_ARSDK_CHANNEL_ID_TRANSPORT:
		/* Make sure buffer is big enough for frame header */
		if (len < header_size) {
			ARSDK_LOGE("transport_mux %p: partial header (%u)",
					self, (uint32_t)len);
			return;
		}

		cdatau8 = cdata;
		/* Decode header */
		if (self->cfg.proto_v == ARSDK_PROTOCOL_VERSION_1) {
			res = decode_header_v1(cdatau8, &header,
					&payloadlen);
			if (res < 0)
				goto error;
			cdatau8 += ARSDK_FRAME_V1_HEADER_SIZE;
		} else {
			res = decode_header_v2(cdatau8, len,
					&header, &header_size, &payloadlen);
			if (res < 0)
				goto error;
			cdatau8 += header_size;
		}

		/* Check header validity */
		if (header_size + payloadlen > len)
			goto error;

		/* Setup payload */
		arsdk_transport_payload_init_with_data(&payload,
				payloadlen == 0 ? NULL : cdatau8,
				payloadlen);
	break;
	default:
		ARSDK_LOGW("unsupported mux channel id %d", chanid);
		return;
	break;
	}

	/* Process data */
	arsdk_transport_recv_data(self->parent, &header, &payload);
	arsdk_transport_payload_clear(&payload);

	return;
error:
	ARSDK_LOGE("transport_net %p: bad frame", self);
	return;
}

/**
 */
static void transport_mux_channel_cb(struct mux_ctx *mux,
		uint32_t chanid,
		enum mux_channel_event event,
		struct pomp_buffer *buf,
		void *userdata)
{
	struct arsdk_transport_mux *self = userdata;
	enum arsdk_link_status status;

	switch (event) {
	case MUX_CHANNEL_RESET:
		ARSDK_LOGI("transport mux channel reset");
		/* set link status KO */
		status = arsdk_transport_get_link_status(self->parent);
		if (status == ARSDK_LINK_STATUS_OK)
			arsdk_transport_set_link_status(self->parent,
					ARSDK_LINK_STATUS_KO);
	break;
	case MUX_CHANNEL_DATA:
		transport_mux_rx_data(self, chanid, buf, userdata);
	break;
	default:
		ARSDK_LOGE("unsupported transport channel event: %d", event);
	break;
	}
}


/**
 */
static int arsdk_transport_mux_dispose(struct arsdk_transport *base)
{
	struct arsdk_transport_mux *self = arsdk_transport_get_child(base);
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	free(self);
	return 0;
}

/**
 */
static int arsdk_transport_mux_start(struct arsdk_transport *base)
{
	int res = 0;
	struct arsdk_transport_mux *self = arsdk_transport_get_child(base);

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	if (self->started)
		return -EBUSY;

	res = mux_channel_open(self->mux, MUX_ARSDK_CHANNEL_ID_TRANSPORT,
				&transport_mux_channel_cb, self);
	if (res < 0) {
		ARSDK_LOG_ERRNO("mux_channel_open", -res);
		return res;
	}

	self->started = 1;
	return 0;
}

/**
 */
static int arsdk_transport_mux_stop(struct arsdk_transport *base)
{
	int res = 0;
	struct arsdk_transport_mux *self = arsdk_transport_get_child(base);

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	if (!self->started)
		return 0;

	res = mux_channel_close(self->mux, MUX_ARSDK_CHANNEL_ID_TRANSPORT);
	if (res < 0)
		ARSDK_LOG_ERRNO("mux_channel_close", -res);

	return 0;
}


static int encode_header_v2(struct pomp_buffer *buf,
		const struct arsdk_transport_header *header,
		uint32_t proto_v, uint32_t payload_len, size_t *headerlen)
{
	int res;
	uint8_t *buf_data;
	size_t buf_len;
	size_t buf_cap;
	uint8_t *data;
	size_t data_len;
	size_t val_len = 0;

	res = pomp_buffer_get_data(buf, (void **)&buf_data, &buf_len, &buf_cap);
	if (res < 0)
		return res;

	data = buf_data + buf_len;
	data_len = buf_cap - buf_len;

	if (proto_v > UINT32_MAX - ARSDK_TRANSPORT_DATA_TYPE_MAX)
		return -EINVAL;
	if (data_len < ARSDK_FRAME_V2_HEADER_SIZE_MAX)
		return -ENOBUFS;

	/* Protocol version */
	res = write_proto_v(data, data_len, proto_v, &val_len);
	if (res < 0)
		return res;

	data += val_len;
	data_len -= val_len;

	data[0] = header->type;
	data++;
	data_len--;

	data[0] = header->id;
	data++;
	data_len--;

	/* Sequence number in 16 bits */
	data[0] = header->seq & 0xff;
	data[1] = (header->seq >> 8) & 0xff;
	data += 2;
	data_len -= 2;

	/* Payload size */
	res = futils_varint_write_u32(data, data_len, payload_len, &val_len);
	if (res < 0)
		return res;
	data += val_len;
	data_len -= val_len;

	*headerlen = data - buf_data - buf_len;

	res = pomp_buffer_set_len(buf, buf_len + *headerlen);
	if (res < 0)
		return res;

	return 0;
}

static int encode_header_v1(struct pomp_buffer *buf,
		const struct arsdk_transport_header *header,
		uint32_t frame_size)
{
	int res;
	uint8_t *data;
	size_t len;
	size_t cap;

	res = pomp_buffer_get_data(buf, (void **)&data, &len, &cap);
	if (res < 0)
		return res;

	if (cap - len < ARSDK_FRAME_V1_HEADER_SIZE)
		return -EINVAL;

	data += len;

	data[0] = header->type;
	data[1] = header->id;
	/* Sequence number in 8 bits */
	data[2] = header->seq;
	/* Frame size number in 32 bits */
	data[3] = frame_size & 0xff;
	data[4] = (frame_size >> 8) & 0xff;
	data[5] = (frame_size >> 16) & 0xff;
	data[6] = (frame_size >> 24) & 0xff;

	res = pomp_buffer_set_len(buf, len + ARSDK_FRAME_V1_HEADER_SIZE);
	if (res < 0)
		return res;

	return 0;
}

/**
 */
static int arsdk_transport_mux_send_data(struct arsdk_transport *base,
		const struct arsdk_transport_header *header,
		const struct arsdk_transport_payload *payload,
		const void *extra_hdr,
		size_t extra_hdrlen)
{
	int res = 0;
	struct arsdk_transport_mux *self = arsdk_transport_get_child(base);
	struct pomp_buffer *buf = NULL;
	size_t header_size = 0;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(header != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(payload != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(extra_hdrlen == 0
			|| extra_hdr != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(payload->len == 0
			|| payload->cdata != NULL, -EINVAL);

	if (!self->started)
		return -EPIPE;

	/* Allocate buffer */
	buf = pomp_buffer_new(ARSDK_FRAME_V2_HEADER_SIZE_MAX);
	if (buf == NULL)
		return -ENOMEM;

	/* Encode header */
	if (self->cfg.proto_v == ARSDK_PROTOCOL_VERSION_1) {
		uint32_t size = ARSDK_FRAME_V1_HEADER_SIZE + extra_hdrlen +
				payload->len;
		res = encode_header_v1(buf, header, size);
		if (res < 0) {
			ARSDK_LOG_ERRNO("encode_header_v1", -res);
			goto out;
		}
	} else {
		res = encode_header_v2(buf, header, self->cfg.proto_v,
				extra_hdrlen + payload->len, &header_size);
		if (res < 0) {
			ARSDK_LOG_ERRNO("encode_header_v2", -res);
			goto out;
		}
	}

	/* Add extra header */
	if (extra_hdrlen > 0) {
		res = pomp_buffer_append_data(buf, extra_hdr, extra_hdrlen);
		if (res < 0) {
			ARSDK_LOG_ERRNO("pomp_buffer_append_data", -res);
			goto out;
		}
	}

	/* Add payload */
	if (payload->len > 0) {
		res = pomp_buffer_append_data(buf,
				payload->cdata, payload->len);
		if (res < 0) {
			ARSDK_LOG_ERRNO("pomp_buffer_append_data", -res);
			goto out;
		}
	}

	/* Send it */
	res = mux_encode(self->mux, MUX_ARSDK_CHANNEL_ID_TRANSPORT, buf);
	if (res < 0) {
		ARSDK_LOGE("mux_encode(chanid=%u): err=%d(%s)",
				MUX_ARSDK_CHANNEL_ID_TRANSPORT,
				res, strerror(-res));
		goto out;
	}

out:
	if (buf != NULL)
		pomp_buffer_unref(buf);
	return res;
}

static uint32_t arsdk_transport_mux_get_proto_v(struct arsdk_transport *base)
{
	struct arsdk_transport_mux *self = arsdk_transport_get_child(base);
	ARSDK_RETURN_VAL_IF_FAILED(self != NULL, -EINVAL, 0);

	return self->cfg.proto_v;
}

/** */
static const struct arsdk_transport_ops s_arsdk_transport_mux_ops = {
	.dispose = &arsdk_transport_mux_dispose,
	.start = &arsdk_transport_mux_start,
	.stop = &arsdk_transport_mux_stop,
	.send_data = &arsdk_transport_mux_send_data,
	.get_proto_v = &arsdk_transport_mux_get_proto_v,
};

/**
 */
int arsdk_transport_mux_new(
		struct mux_ctx *mux,
		struct pomp_loop *loop,
		const struct arsdk_transport_mux_cfg *cfg,
		struct arsdk_transport_mux **ret_obj)
{
	int res = 0;
	struct arsdk_transport_mux *self = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(loop != NULL, -EINVAL);

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Setup base structure */
	res = arsdk_transport_new(self, &s_arsdk_transport_mux_ops, loop,
			ARSDK_TRANSPORT_PING_PERIOD, ARSDK_TRANSPORT_TAG,
			&self->parent);
	if (res < 0)
		goto error;

	/* Initialize structure */
	self->mux = mux;
	self->loop = loop;
	self->cfg = *cfg;

	/* Success */
	*ret_obj = self;
	return 0;

	/* TODO: Cleanup in case of error */
error:
	return res;
}

/**
 */
struct arsdk_transport *arsdk_transport_mux_get_parent(
		struct arsdk_transport_mux *self)
{
	return self == NULL ? NULL : self->parent;
}

/**
 */
int arsdk_transport_mux_get_cfg(struct arsdk_transport_mux *self,
		struct arsdk_transport_mux_cfg *cfg)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg != NULL, -EINVAL);
	*cfg = self->cfg;
	return 0;
}

/**
 */
int arsdk_transport_mux_update_cfg(struct arsdk_transport_mux *self,
		const struct arsdk_transport_mux_cfg *cfg)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg != NULL, -EINVAL);
	/* TODO: check that only tx fields are changed */
	self->cfg = *cfg;
	return 0;
}

#endif /* BUILD_LIBMUX */
