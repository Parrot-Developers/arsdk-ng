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

#ifndef _ARSDK_BLACKBOX_ITF_H_
#define _ARSDK_BLACKBOX_ITF_H_

struct arsdk_blackbox_listener;

/** blackbox remote controller piloting info */
struct arsdk_blackbox_rc_piloting_info {
	int8_t pitch;   /**< pitch value*/
	int8_t roll;    /**< roll value*/
	int8_t yaw;     /**< yaw value*/
	int8_t gaz;     /**< gaz value*/
	int8_t source;  /**< indicates which pcmd is sent between this values or
			     the application's pcmd*/
};

/** blackbox interface callbacks */
struct arsdk_blackbox_listener_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Notify remote controller button action.
	 * @param itf : the blackbox interface.
	 * @param listener : the listener.
	 * @param action : action.
	 * @param userdata : user data.
	 */
	void (*rc_button_action)(struct arsdk_blackbox_itf *itf,
			struct arsdk_blackbox_listener *listener,
			int action,
			void *userdata);

	/**
	 * Notify remote controller piloting info.
	 * @param itf : the blackbox interface.
	 * @param listener : the listener.
	 * @param info : piloting_info.
	 * @param userdata : user data.
	 */
	void (*rc_piloting_info)(struct arsdk_blackbox_itf *itf,
			struct arsdk_blackbox_listener *listener,
			struct arsdk_blackbox_rc_piloting_info *info,
			void *userdata);

	/**
	 * Notify listener is unregistered and should no longer be used.
	 * @param itf : the blackbox interface.
	 * @param listener : the listener.
	 * @param info : piloting_info.
	 * @param userdata : user data.
	 */
	void (*unregister)(struct arsdk_blackbox_itf *itf,
			struct arsdk_blackbox_listener *listener,
			void *userdata);
};

/**
 * Create a blackbox listener.
 * @param itf : the blackbox interface.
 * @param cbs : listener callback.
 * @param ret_obj : will receive the listener object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_blackbox_itf_create_listener(struct arsdk_blackbox_itf *itf,
		struct arsdk_blackbox_listener_cbs *cbs,
		struct arsdk_blackbox_listener **ret_obj);

/**
 * Unregister a blackbox listener.
 * @param listener : the listener to unregister.
 * @return 0 in case of success, negative errno value in case of error.
 * @note the listener should no be used after this call.
 */
ARSDK_API int arsdk_blackbox_listener_unregister(
		struct arsdk_blackbox_listener *listener);

#endif /* !_ARSDK_BLACKBOX_ITF_H_ */
