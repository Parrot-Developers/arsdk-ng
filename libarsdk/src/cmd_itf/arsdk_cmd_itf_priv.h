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

/** Informations of transmission queue */
struct arsdk_cmd_queue_info {
	/** Type of data in the queue. */
	enum arsdk_transport_data_type  type;
	/** Queue identifier. */
	uint8_t                         id;
	/**
	 * Maximum rate of send in millisecond.
	 * '0' to send soon as possible.
	 * Not used since the version 2. Sending always soon as possible.*/
	int                             max_tx_rate_ms;
	/**
	 * Time to wait after retry to send an acknowledged command;
	 * in millisecond.
	 * Not used if 'type' is not 'ARSDK_TRANSPORT_DATA_TYPE_WITHACK'.
	 */
	int                             ack_timeout_ms;
	/**
	 * Different of '0' to allow to overwrite old pending commands
	 * in the queue.
	 * Not used since the version 2; overwriting not more allowed.
	 */
	int                             overwrite;
	/**
	 * Maximum count of retry before to drop a command.
	 * '-1' for infinite retry.
	 * Not used since the version 2; forced in infinite retry.
	 */
	int32_t                         default_max_retry_count;
};

/** Command interface internal callbacks. */
struct arsdk_cmd_itf_internal_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Function called when the command interface is disposed.
	 *
	 * @param itf : interface object.
	 * @param userdata : user data.
	 */
	int (*dispose)(struct arsdk_cmd_itf *itf, void *userdata);
};

/**
 * Creates a new command interface
 *
 * @param transport : Transport used to send commands.
 * @param cbs : Interface callbacks.
 * @param internal_cbs : Interface internal callbacks.
 * @param tx_info_table : transmission queues information.
 * @param tx_count : length of 'tx_info_table'.
 * @param ackoff : Index offset between a transmission queue and
 *		   its reception acknowledge.
 * @param[out] ret_itf : will receive the command interface object.
 *
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_cmd_itf_new(struct arsdk_transport *transport,
		const struct arsdk_cmd_itf_cbs *cbs,
		const struct arsdk_cmd_itf_internal_cbs *internal_cbs,
		const struct arsdk_cmd_queue_info *tx_info_table,
		uint32_t tx_count,
		uint8_t ackoff,
		struct arsdk_cmd_itf **ret_itf);

/**
 * Destroys an command interface
 *
 * @param itf : Command interface to destroy.
 *
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_cmd_itf_destroy(struct arsdk_cmd_itf *itf);

/**
 * Stops the interface.
 *
 * Cancel all pending commands.
 *
 * @param itf : Command interface to stop.
 *
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_cmd_itf_stop(struct arsdk_cmd_itf *itf);

/**
 * Notifies data received to the interface.
 *
 * @param itf : The command interface.
 * @param header : data header.
 * @param payload : data payload.
 *
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_cmd_itf_recv_data(struct arsdk_cmd_itf *itf,
		const struct arsdk_transport_header *header,
		const struct arsdk_transport_payload *payload);

#endif /* !_ARSDK_CMD_ITF_PRIV_H_ */
