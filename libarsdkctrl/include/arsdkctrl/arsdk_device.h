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

#ifndef _ARSDK_DEVICE_H_
#define _ARSDK_DEVICE_H_

#define ARSDK_DEVICE_INVALID_HANDLE 0

/**
 * Device state.
 */
enum arsdk_device_state {
	ARSDK_DEVICE_STATE_IDLE,                /**< Idle (disconnected) */
	ARSDK_DEVICE_STATE_CONNECTING,          /**< Connecting */
	ARSDK_DEVICE_STATE_CONNECTED,           /**< Connected */
	ARSDK_DEVICE_STATE_REMOVING,            /**< Removing in progress */
};

/**
 * Device API capabilities.
 */
enum arsdk_device_api {
	/** API capabilities unknown. */
	ARSDK_DEVICE_API_UNKNOWN,
	/** Full API supported. */
	ARSDK_DEVICE_API_FULL,
	/** Update API only. */
	ARSDK_DEVICE_API_UPDATE_ONLY,
};

/**
 * Device information.
 */
struct arsdk_device_info {
	enum arsdk_backend_type  backend_type;  /**< Underlying backend type */
	uint32_t                 proto_v;       /**< Protocol version */
	enum arsdk_device_api    api;           /**< API capabilities */
	enum arsdk_device_state  state;         /**< State */
	const char               *name;         /**< Name */
	enum arsdk_device_type   type;          /**< Type */
	const char               *addr;         /**< Address */
	uint16_t                 port;          /**< Port */
	const char               *id;           /**< Id */
	const char               *json;         /**< Json received */
};

/**
 * Device connection configuration.
 */
struct arsdk_device_conn_cfg {
	const char               *ctrl_name;    /**< Controller name */
	const char               *ctrl_type;    /**< Controller type */
	const char               *device_id;    /**< Requested device id */
	const char               *json;         /**< Json to send */
};

/**
 * Device connection callbacks.
 */
struct arsdk_device_conn_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Notify connection initiation.
	 * @param device : device object.
	 * @param info : device information.
	 * @param userdata : user data.
	 */
	void (*connecting)(struct arsdk_device *device,
			const struct arsdk_device_info *info,
			void *userdata);

	/**
	 * Notify connection completion.
	 * @param device : device object.
	 * @param info : device information.
	 * @param userdata : user data.
	 */
	void (*connected)(struct arsdk_device *device,
			const struct arsdk_device_info *info,
			void *userdata);

	/**
	 * Notify disconnection.
	 * @param device : device object.
	 * @param info : device information.
	 * @param userdata : user data.
	 */
	void (*disconnected)(struct arsdk_device *device,
			const struct arsdk_device_info *info,
			void *userdata);

	/**
	 * Notify connection cancellation. Either because 'disconnect' was
	 * called before 'connected' callback or remote aborted/rejected the
	 * request.
	 * @param device : device object.
	 * @param info : device information.
	 * @param reason : reason of cancellation.
	 * @param userdata : user data.
	 */
	void (*canceled)(struct arsdk_device *device,
			const struct arsdk_device_info *info,
			enum arsdk_conn_cancel_reason reason,
			void *userdata);

	/**
	 * Notify link status. At connection completion, it is assumed to be
	 * initially OK. If called with KO, user is responsible to take action.
	 * It can either wait for link to become OK again or disconnect
	 * immediately. In this case, call arsdk_device_disconnect and the
	 * 'disconnected' callback will be called.
	 * @param device : device object.
	 * @param info : device information.
	 * @param status : link status.
	 * @param userdata : user data.
	 */
	void (*link_status)(struct arsdk_device *device,
			const struct arsdk_device_info *info,
			enum arsdk_link_status status,
			void *userdata);
};

ARSDK_API const char *arsdk_device_state_str(enum arsdk_device_state val);

/**
 * Get device handle.
 * @param self : device object.
 * @return device handle in case of success, 0 value in case of error.
 */
ARSDK_API uint16_t arsdk_device_get_handle(struct arsdk_device *self);

/**
 * Retrieve information about the device object.
 * @param self : device object.
 * @param info : will receive a pointer to the internal information structure.
 * User can assume that the returned pointer will be valid as long as the device
 * object is itself valid. However the characters strings pointers may change
 * at any time after the call.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_device_get_info(struct arsdk_device *self,
		const struct arsdk_device_info **info);

/**
 * Get underlying backend.
 * @param self : device object.
 * @return underlying backend or NULL if backend has been destroyed while some
 * device references were still present.
 */
ARSDK_API struct arsdkctrl_backend *arsdk_device_get_backend(
		struct arsdk_device *self);

/**
 * Initiate connection with a device object.
 * @param self : device object.
 * @param cfg : connection configuration.
 * @param cbs : connection callbacks.
 * @param loop : event loop to use for the connection.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_device_connect(struct arsdk_device *self,
		const struct arsdk_device_conn_cfg *cfg,
		const struct arsdk_device_conn_cbs *cbs,
		struct pomp_loop *loop);

/**
 * Disconnect (or cancel pending connection) with device object.
 * @param self : device object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_device_disconnect(struct arsdk_device *self);

/**
 * Create the command interface to communicate with the device object.
 * @param self : device object.
 * @param cbs : command interface callbacks.
 * @param ret_itf : will receive the command interface object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_device_create_cmd_itf(
		struct arsdk_device *self,
		const struct arsdk_cmd_itf_cbs *cbs,
		struct arsdk_cmd_itf **ret_itf);

/**
 * Get the device command interface previously created by
 * arsdk_device_create_cmd_itf.
 * @param self : device object.
 * @return command interface instance or null it the device doesn't have
 * a command interface created.
 */
ARSDK_API struct arsdk_cmd_itf *arsdk_device_get_cmd_itf(
		struct arsdk_device *self);

/**
 * Get the ftp interface to communicate with the device object.
 * @param self : device object.
 * @param ret_itf : will receive the ftp interface object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_device_get_ftp_itf(
		struct arsdk_device *self,
		struct arsdk_ftp_itf **ret_itf);

/**
 * Get the media interface to communicate with the device object.
 * @param self : device object.
 * @param ret_itf : will receive the media interface object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_device_get_media_itf(
		struct arsdk_device *self,
		struct arsdk_media_itf **ret_itf);

/**
 * Get the updater interface to update a device object.
 * @param self : device object.
 * @param ret_itf : will receive the updater interface object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_device_get_updater_itf(
		struct arsdk_device *self,
		struct arsdk_updater_itf **ret_itf);

/**
 * Get the blackbox interface.
 * @param self : device object.
 * @param ret_itf : will receive the blackbox interface object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_device_get_blackbox_itf(
		struct arsdk_device *self,
		struct arsdk_blackbox_itf **ret_itf);

/**
 * Get the crashml interface to download all crashml from the device.
 * @param self : device object.
 * @param ret_itf : will receive the crashml interface object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_device_get_crashml_itf(
		struct arsdk_device *self,
		struct arsdk_crashml_itf **ret_itf);

/**
 * Get the flight log interface to download all flight log from the device.
 * @param self : device object.
 * @param ret_itf : will receive the flight log interface object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_device_get_flight_log_itf(
		struct arsdk_device *self,
		struct arsdk_flight_log_itf **ret_itf);

/**
 * Get the pud interface to download all pud from the device.
 * @param self : device object.
 * @param ret_itf : will receive the pud interface object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_device_get_pud_itf(
		struct arsdk_device *self,
		struct arsdk_pud_itf **ret_itf);

/**
 * Get the ephemeris interface to upload ephemeris files on the device.
 * @param self : device object.
 * @param ret_itf : will receive the ephemeris interface object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_device_get_ephemeris_itf(
		struct arsdk_device *self,
		struct arsdk_ephemeris_itf **ret_itf);

/** Device tcp proxy event callbacks */
struct arsdk_device_tcp_proxy_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Function called at the local socket opening.
	 *
	 * @param self : Proxy object.
	 * @param localport : Proxy local socket port.
	 * @param userdata : User data.
	 */
	void (*open)(struct arsdk_device_tcp_proxy *self, uint16_t localport,
			void *userdata);

	/**
	 * Function called at the local socket closing.
	 *
	 * @param self : Proxy object.
	 * @param userdata : User data.
	 */
	void (*close)(struct arsdk_device_tcp_proxy *self, void *userdata);
};

/**
 * Create a tcp proxy to a device
 * @param self : device object.
 * @param dev_type : type of the device to access.
 * @param port : port to access.
 * @param cbs : proxy event callbacks.
 * @param ret_proxy : will receive the tcp proxy object.
 * @return 0 in case of success, negative errno value in case of error.
 * @note arsdk_device_destroy_tcp_proxy must be call to destroy the proxy.
 */
ARSDK_API int arsdk_device_create_tcp_proxy(struct arsdk_device *self,
		enum arsdk_device_type dev_type,
		uint16_t port,
#ifndef LIBMUX_LEGACY
		struct arsdk_device_tcp_proxy_cbs *cbs,
#endif
		struct arsdk_device_tcp_proxy **ret_proxy);

/**
 * Destroy a tcp proxy
 * @param proxy : the tcp proxy to destroy.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_device_destroy_tcp_proxy(
		struct arsdk_device_tcp_proxy *proxy);

/**
 * Get device tcp proxy address.
 * @param proxy : device tcp proxy.
 * @return address of the proxy in case of success, NULL value in case of error.
 */
ARSDK_API const char *arsdk_device_tcp_proxy_get_addr(
		struct arsdk_device_tcp_proxy *proxy);

/**
 * Get device tcp proxy port.
 * @param proxy : device tcp proxy.
 * @return port of the proxy or '0' if the proxy is not opened,
 *         negative errno value in case of error.
 */
ARSDK_API int arsdk_device_tcp_proxy_get_port(
		struct arsdk_device_tcp_proxy *proxy);

#endif /* _ARSDK_DEVICE_H_ */
