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

#ifndef _ARSDK_PEER_H_
#define _ARSDK_PEER_H_

/**
 * Peer information.
 */
struct arsdk_peer_info {
	enum arsdk_backend_type  backend_type;  /**< Underlying backend type */
	uint32_t                 proto_v;       /**< Protocol version */
	const char               *ctrl_name;    /**< Controller name */
	const char               *ctrl_type;    /**< Controller type */
	const char               *ctrl_addr;    /**< Controller address */
	const char               *device_id;    /**< Requested device Id */
	const char               *json;         /**< Json received */
};

/**
 * Peer connection configuration.
 */
struct arsdk_peer_conn_cfg {
	const char            *json;       /**< Json to send */
};

/**
 * Peer connection callbacks.
 */
struct arsdk_peer_conn_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Notify connection completion.
	 * @param peer : peer object.
	 * @param info : peer information.
	 * @param userdata : user data.
	 */
	void (*connected)(struct arsdk_peer *peer,
			const struct arsdk_peer_info *info,
			void *userdata);

	/**
	 * Notify disconnection.
	 * @param peer : peer object.
	 * @param info : peer information.
	 * @param userdata : user data.
	 */
	void (*disconnected)(struct arsdk_peer *peer,
			const struct arsdk_peer_info *info,
			void *userdata);

	/**
	 * Notify connection cancellation. Either because 'disconnect' was
	 * called before 'connected' callback or remote aborted/rejected the
	 * request.
	 * @param peer : peer object.
	 * @param info : peer information.
	 * @param reason : reason of cancellation.
	 * @param userdata : user data.
	 */
	void (*canceled)(struct arsdk_peer *peer,
			const struct arsdk_peer_info *info,
			enum arsdk_conn_cancel_reason reason,
			void *userdata);

	/**
	 * Notify link status. At connection completion, it is assumed to be
	 * initially OK. If called with KO, user is responsible to take action.
	 * It can either wait for link to become OK again or disconnect
	 * immediately. In this case, call arsdk_peer_disconnect and the
	 * 'disconnected' callback will be called.
	 * @param peer : peer object.
	 * @param info : peer information.
	 * @param status : link status.
	 * @param userdata : user data.
	 */
	void (*link_status)(struct arsdk_peer *peer,
			const struct arsdk_peer_info *info,
			enum arsdk_link_status status,
			void *userdata);
};

/**
 * Get peer handle.
 * @param self : peer object.
 * @return peer handle in case of success, 0 value in case of error.
 */
ARSDK_API uint16_t arsdk_peer_get_handle(struct arsdk_peer *self);

/**
 * Retrieve information about the peer object.
 * @param self : peer object.
 * @param info : will receive a pointer to the internal information structure.
 * User can assume that the returned pointer will be valid as long as the peer
 * object is itself valid. However the characters strings pointers may change
 * at any time after the call.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_peer_get_info(struct arsdk_peer *self,
		const struct arsdk_peer_info **info);

/**
 * Get underlying backend.
 * @param self : device object.
 * @return underlying backend or NULL if backend has been destroyed while some
 * device references were still present.
 */
ARSDK_API struct arsdk_backend *arsdk_peer_get_backend(
		struct arsdk_peer *self);

/**
 * Accept connection with peer object.
 * @param self : peer object.
 * @param cfg : connection configuration.
 * @param cbs : connection callbacks.
 * @param loop : event loop to use for the connection.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_peer_accept(struct arsdk_peer *self,
		const struct arsdk_peer_conn_cfg *cfg,
		const struct arsdk_peer_conn_cbs *cbs,
		struct pomp_loop *loop);

/**
 * Reject connection with peer object.
 * @param self : peer object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_peer_reject(struct arsdk_peer *self);

/**
 * Disconnect (or cancel pending connection) with peer object.
 * @param self : peer object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_peer_disconnect(struct arsdk_peer *self);

/**
 * Create the command interface to communicate with the peer object.
 * @param self : peer object.
 * @param cbs : command interface callbacks.
 * @param ret_itf : will receive the command interface object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_peer_create_cmd_itf(
		struct arsdk_peer *self,
		const struct arsdk_cmd_itf_cbs *cbs,
		struct arsdk_cmd_itf **ret_itf);

/**
 * Get the peer command interface previously created by
 * arsdk_peer_create_cmd_itf.
 * @param self : device object.
 * @return command interface instance or null it the peer doesn't have
 * a command interface created.
 */
ARSDK_API struct arsdk_cmd_itf *arsdk_peer_get_cmd_itf(
		struct arsdk_peer *self);

#endif /* _ARSDK_PEER_H_ */
