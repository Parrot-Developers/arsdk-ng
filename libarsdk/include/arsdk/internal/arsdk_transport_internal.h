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

#ifndef _ARSDK_TRANSPORT_INTERNAL_H_
#define _ARSDK_TRANSPORT_INTERNAL_H_

/** Data type */
enum arsdk_transport_data_type {
	ARSDK_TRANSPORT_DATA_TYPE_UNKNOWN = 0,
	ARSDK_TRANSPORT_DATA_TYPE_ACK,
	ARSDK_TRANSPORT_DATA_TYPE_NOACK,
	ARSDK_TRANSPORT_DATA_TYPE_LOWLATENCY,
	ARSDK_TRANSPORT_DATA_TYPE_WITHACK,

	/** Maximum value ; Should not be changed. */
	ARSDK_TRANSPORT_DATA_TYPE_MAX = 10,
};

/** */
struct arsdk_transport_header {
	enum arsdk_transport_data_type  type;
	uint8_t                         id;
	uint16_t                        seq;
};

/** */
struct arsdk_transport_payload {
	struct pomp_buffer  *buf;
	const void          *cdata;
	size_t              len;
};

/** */
struct arsdk_transport_cbs {
	void *userdata;

	void (*recv_data)(struct arsdk_transport *transport,
			const struct arsdk_transport_header *header,
			const struct arsdk_transport_payload *payload,
			void *userdata);

	void (*link_status)(struct arsdk_transport *transport,
			enum arsdk_link_status status,
			void *userdata);

	void (*log_cb)(struct arsdk_transport *transport,
			enum arsdk_cmd_dir dir,
			const void *header,
			size_t headerlen,
			const void *payload,
			size_t payloadlen,
			void *userdata);
};

/** Transport operations */
struct arsdk_transport_ops {
	/**
	 * Disposes the transport.
	 *
	 * @param base : Transport base.
	 *
	 * @return 0 in case of success, negative errno value in case of error.
	 */
	int (*dispose)(struct arsdk_transport *base);

	/**
	 * Starts the transport.
	 *
	 * @param base : Transport base.
	 *
	 * @return 0 in case of success, negative errno value in case of error.
	 */
	int (*start)(struct arsdk_transport *base);

	/**
	 * Stops the transport.
	 *
	 * @param base : Transport base.
	 *
	 * @return 0 in case of success, negative errno value in case of error.
	 */
	int (*stop)(struct arsdk_transport *base);

	/**
	 * Send data by the transport.
	 *
	 * @param base : Transport base.
	 * @param header : Data header.
	 * @param payload : Data payload.
	 * @param extra_hdr : Extra data header.
	 * @param extra_hdrlen : Extra data header length.
	 *
	 * @return 0 in case of success, negative errno value in case of error.
	 */
	int (*send_data)(struct arsdk_transport *base,
			const struct arsdk_transport_header *header,
			const struct arsdk_transport_payload *payload,
			const void *extra_hdr,
			size_t extra_hdrlen);

	/**
	 * Retrives protocol version.
	 *
	 * @param base : Transport base.
	 *
	 * @remarks: Default implementation returns 'ARSDK_PROTOCOL_VERSION_1'.
	 */
	uint32_t (*get_proto_v)(struct arsdk_transport *base);
};

ARSDK_API int arsdk_transport_new(
		void *child,
		const struct arsdk_transport_ops *ops,
		struct pomp_loop *loop,
		uint32_t ping_period,
		const char *name,
		struct arsdk_transport **ret_obj);

ARSDK_API int arsdk_transport_destroy(
		struct arsdk_transport *self);

ARSDK_API void *arsdk_transport_get_child(
		struct arsdk_transport *self);

ARSDK_API struct pomp_loop *arsdk_transport_get_loop(
		struct arsdk_transport *self);

ARSDK_API int arsdk_transport_start(
		struct arsdk_transport *self,
		const struct arsdk_transport_cbs *cbs);

ARSDK_API int arsdk_transport_stop(
		struct arsdk_transport *self);

ARSDK_API int arsdk_transport_send_data(
		struct arsdk_transport *self,
		const struct arsdk_transport_header *header,
		const struct arsdk_transport_payload *payload,
		const void *extra_hdr,
		size_t extra_hdrlen);

ARSDK_API int arsdk_transport_recv_data(
		struct arsdk_transport *self,
		const struct arsdk_transport_header *header,
		const struct arsdk_transport_payload *payload);

ARSDK_API int arsdk_transport_set_link_status(
		struct arsdk_transport *self,
		enum arsdk_link_status status);

ARSDK_API enum arsdk_link_status arsdk_transport_get_link_status(
		struct arsdk_transport *self);

ARSDK_API void arsdk_transport_log_cmd(
		struct arsdk_transport *self,
		const void *header,
		size_t headerlen,
		const struct arsdk_transport_payload *payload,
		enum arsdk_cmd_dir dir);

ARSDK_API uint32_t arsdk_transport_get_proto_v(struct arsdk_transport *self);

/**
 */
static inline void arsdk_transport_payload_init(
		struct arsdk_transport_payload *payload)
{
	memset(payload, 0, sizeof(*payload));
}

/** */
static inline void arsdk_transport_payload_init_with_buf(
		struct arsdk_transport_payload *payload,
		struct pomp_buffer *buf)
{
	memset(payload, 0, sizeof(*payload));
	payload->buf = buf;
	pomp_buffer_ref(buf);
	pomp_buffer_get_cdata(buf, &payload->cdata, &payload->len, NULL);
}

/** */
static inline void arsdk_transport_payload_init_with_data(
		struct arsdk_transport_payload *payload,
		const void *cdata, size_t len)
{
	memset(payload, 0, sizeof(*payload));
	payload->cdata = cdata;
	payload->len = len;
}

/** */
static inline void arsdk_transport_payload_clear(
		struct arsdk_transport_payload *payload)
{
	if (payload->buf != NULL)
		pomp_buffer_unref(payload->buf);
	memset(payload, 0, sizeof(*payload));
}

#endif /* _ARSDK_TRANSPORT_INTERNAL_H_ */
