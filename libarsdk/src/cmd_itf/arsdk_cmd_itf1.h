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

#ifndef _ARSDK_CMD_ITF1_H_
#define _ARSDK_CMD_ITF1_H_

/** Forward declarations */
struct arsdk_cmd_itf_cbs;
struct arsdk_cmd_itf;
struct arsdk_cmd_itf1;

/**
 * Command interface callbacks.
 */
struct arsdk_cmd_itf1_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Function called when command interface is going to be destroyed.
	 * All references to this interface must be removed.
	 *
	 * @param itf1 : interface object.
	 * @param userdata : user data.
	 */
	void (*dispose)(struct arsdk_cmd_itf1 *itf1, void *userdata);
};

/**
 * Creates a new command interface version 1.
 *
 * @param transport : Transport used to send commands.
 * @param cbs : Interface V1 callbacks.
 * @param itf_cbs : Interface parent callbacks.
 * @param itf : Interface parent.
 * @param tx_info_table : transmission queues information.
 * @param tx_count : length of 'tx_info_table'.
 * @param ackoff : Index offset between a transmission queue and
 *		   its reception acknowledge.
 * @param[out] ret_itf : will receive the command interface V1 object.
 *
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_cmd_itf1_new(struct arsdk_transport *transport,
		const struct arsdk_cmd_itf1_cbs *cbs,
		const struct arsdk_cmd_itf_cbs *itf_cbs,
		struct arsdk_cmd_itf *itf,
		const struct arsdk_cmd_queue_info *tx_info_table,
		uint32_t tx_count,
		uint8_t ackoff,
		struct arsdk_cmd_itf1 **ret_obj);

/**
 * Destroys an command interface version 1.
 *
 * @param itf : Command interface to destroy.
 *
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_cmd_itf1_destroy(struct arsdk_cmd_itf1 *self);

/**
 * Sends a command.
 *
 * @param self : interface object.
 * @param cmd : command structure.
 * @param send_status : function to call with send status. If NULL, the one
 * given at creation will be used.
 * @param userdata : user data for send_status callback.
 *
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_cmd_itf1_send(struct arsdk_cmd_itf1 *self,
		const struct arsdk_cmd *cmd,
		arsdk_cmd_itf_send_status_cb_t send_status,
		void *userdata);

/**
 * Stops the interface.
 *
 * Cancel all pending commands.
 *
 * @param self : Command interface to stop.
 *
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_cmd_itf1_stop(struct arsdk_cmd_itf1 *self);

/**
 * Notifies data received to the interface.
 *
 * @param self :        Ccommand interface.
 * @param header :      Data header.
 * @param payload :     Data payload.
 *
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_cmd_itf1_recv_data(struct arsdk_cmd_itf1 *self,
		const struct arsdk_transport_header *header,
		const struct arsdk_transport_payload *payload);

#endif /* !_ARSDK_CMD_ITF1_H_ */
