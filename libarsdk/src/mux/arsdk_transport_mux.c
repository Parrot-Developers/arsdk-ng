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

#define ARSDK_FRAME_HEADER_SIZE      7
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
 */
static void transport_mux_rx_data(struct arsdk_transport_mux *transport,
		uint32_t chanid,
		struct pomp_buffer *buf,
		void *userdata)
{
	const void *cdata = NULL;
	const uint8_t *cdatau8 = NULL;
	size_t len = 0;
	struct arsdk_transport_header header;
	struct arsdk_transport_payload payload;
	uint32_t size = 0, payloadlen = 0;

	/* Get data from buffer */
	if (pomp_buffer_get_cdata(buf, &cdata, &len, NULL) < 0)
		return;

	memset(&header, 0, sizeof(header));

	switch (chanid) {
	case MUX_ARSDK_CHANNEL_ID_TRANSPORT:
		/* Make sure buffer is big enough for frame header */
		if (len < ARSDK_FRAME_HEADER_SIZE) {
			ARSDK_LOGE("transport_mux %p: partial header (%u)",
					transport, (uint32_t)len);
			return;
		}

		/* Decode header */
		cdatau8 = cdata;
		header.type = cdatau8[0];
		header.id = cdatau8[1];
		header.seq = cdatau8[2];
		size = cdatau8[3] | (cdatau8[4] << 8) | (cdatau8[5] << 16) |
				(cdatau8[6] << 24);

		/* Check header validity */
		if (size < ARSDK_FRAME_HEADER_SIZE || size > len) {
			ARSDK_LOGE("transport_mux %p: bad frame", transport);
			return;
		}
		cdatau8 += ARSDK_FRAME_HEADER_SIZE;

		/* Setup payload */
		payloadlen = size - ARSDK_FRAME_HEADER_SIZE;
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
	arsdk_transport_recv_data(transport->parent, &header, &payload);
	arsdk_transport_payload_clear(&payload);
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
	uint32_t size = 0;
	struct pomp_buffer *buf = NULL;
	void *data = NULL;
	uint8_t *datau8 = NULL;

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
	size = ARSDK_FRAME_HEADER_SIZE + extra_hdrlen + payload->len;
	buf = pomp_buffer_new_get_data(size, &data);
	if (buf == NULL)
		return -ENOMEM;
	datau8 = data;

	/* Construct header */
	datau8[0] = header->type;
	datau8[1] = header->id;
	datau8[2] = header->seq;
	datau8[3] = size & 0xff;
	datau8[4] = (size >> 8) & 0xff;
	datau8[5] = (size >> 16) & 0xff;
	datau8[6] = (size >> 24) & 0xff;
	datau8 += ARSDK_FRAME_HEADER_SIZE;

	/* Add extra header */
	if (extra_hdrlen > 0) {
		memcpy(datau8, extra_hdr, extra_hdrlen);
		datau8 += extra_hdrlen;
	}

	/* Add payload */
	if (payload->len > 0) {
		memcpy(datau8, payload->cdata, payload->len);
		datau8 += payload->len;
	}

	/* Send it */
	res = pomp_buffer_set_len(buf, size);
	if (res < 0)
		goto out;
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

/** */
static const struct arsdk_transport_ops s_arsdk_transport_mux_ops = {
	.dispose = &arsdk_transport_mux_dispose,
	.start = &arsdk_transport_mux_start,
	.stop = &arsdk_transport_mux_stop,
	.send_data = &arsdk_transport_mux_send_data,
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
	res = arsdk_transport_new(self, &s_arsdk_transport_mux_ops,
			loop, ARSDK_TRANSPORT_PING_PERIOD, ARSDK_TRANSPORT_TAG,
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

#endif /* BUILD_LIBMUX */
