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

#ifndef _ARSDK_MNGR_H_
#define _ARSDK_MNGR_H_


#define ARSDK_INVALID_HANDLE 0

/**
 * manager peer callbacks.
 */
struct arsdk_mngr_peer_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Notify device added.
	 * @param device : device added.
	 * @param userdata :  user data.
	 */
	void (*added) (struct arsdk_peer *peer, void *userdata);

	/**
	 * Notify peer removed.
	 * @param peer : peer added.
	 * @param userdata :  user data.
	 */
	void (*removed) (struct arsdk_peer *peer, void *userdata);
};

/**
 * Create a manager.
 * @param loop : manager pomp loop.
 * @param ret_mngr : will receive the manager object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_mngr_new(struct pomp_loop *loop,
		struct arsdk_mngr **ret_mngr);

/**
 * Set manager peer cbs.
 * @param mngr : manager.
 * @param cbs : peer callbacks.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_mngr_set_peer_cbs(struct arsdk_mngr *mngr,
		const struct arsdk_mngr_peer_cbs *cbs);

/**
 * Get manager pomp_loop.
 * @param mngr : manager.
 * @return pomp_loop in case of success, NULL in case of error.
 */
ARSDK_API struct pomp_loop *arsdk_mngr_get_loop(struct arsdk_mngr *mngr);

/**
 * Destroy manager cbs.
 * @param mngr : manager.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_mngr_destroy(struct arsdk_mngr *mngr);

/**
 * Iterate through manager peers list.
 * @param mngr : manager.
 * @param prev : previous peer in list (NULL to start from the beginnin).
 * @return next peer in list or NULL if no more device.
 */
ARSDK_API struct arsdk_peer *arsdk_mngr_next_peer(struct arsdk_mngr *mngr,
		struct arsdk_peer *prev);

/**
 * Get peer from handle.
 * @param mngr : manager.
 * @param handle : peer handle.
 * @return peer or NULL if not found.
 */
ARSDK_API struct arsdk_peer *arsdk_mngr_get_peer(struct arsdk_mngr *mngr,
		uint16_t handle);

#endif /* !_ARSDK_MNGR_H_ */
