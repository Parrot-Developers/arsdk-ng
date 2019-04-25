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

#ifndef _ARSDK_CMD_ITF_PRIV_H_
#define _ARSDK_CMD_ITF_PRIV_H_

/** */
struct arsdk_cmd_queue_info {
	enum arsdk_transport_data_type  type;
	uint8_t                         id;
	int                             max_tx_rate_ms;
	int                             ack_timeout_ms;
	int                             overwrite;
	int32_t                         default_max_retry_count;
};

/** */
struct arsdk_cmd_itf_internal_cbs {
	void *userdata;

	int (*dispose)(struct arsdk_cmd_itf *itf,
			void *userdata);
};

ARSDK_API int arsdk_cmd_itf_new(struct arsdk_transport *transport,
		const struct arsdk_cmd_itf_cbs *cbs,
		const struct arsdk_cmd_itf_internal_cbs *internal_cbs,
		const struct arsdk_cmd_queue_info *tx_info_table,
		uint32_t tx_count,
		uint8_t ackoff,
		struct arsdk_cmd_itf **ret_itf);

ARSDK_API int arsdk_cmd_itf_destroy(struct arsdk_cmd_itf *itf);

ARSDK_API int arsdk_cmd_itf_stop(struct arsdk_cmd_itf *itf);

ARSDK_API int arsdk_cmd_itf_recv_data(struct arsdk_cmd_itf *itf,
		const struct arsdk_transport_header *header,
		const struct arsdk_transport_payload *payload);


#endif /* !_ARSDK_CMD_ITF_PRIV_H_ */
