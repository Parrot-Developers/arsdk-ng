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

#ifndef _ARSDK_CTRL_H_
#define _ARSDK_CTRL_H_


#define ARSDK_INVALID_HANDLE 0

/**
 * manager device callbacks.
 */
struct arsdk_ctrl_device_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Notify device added.
	 * @param device : device added.
	 * @param userdata :  user data.
	 */
	void (*added) (struct arsdk_device *device, void *userdata);

	/**
	 * Notify device removed.
	 * @param device : device added.
	 * @param userdata :  user data.
	 */
	void (*removed) (struct arsdk_device *device, void *userdata);
};

/**
 * Create a controller.
 * @param loop : manager pomp loop.
 * @param ret_ctrl : will receive the cotroller object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_ctrl_new(struct pomp_loop *loop,
		struct arsdk_ctrl **ret_ctrl);

/**
 * Set controller device cbs.
 * @param ctrl : controller.
 * @param cbs : device callbacks.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_ctrl_set_device_cbs(struct arsdk_ctrl *ctrl,
		const struct arsdk_ctrl_device_cbs *cbs);

/**
 * Get manager pomp_loop.
 * @param ctrl : controller.
 * @return pomp_loop in case of success, NULL in case of error.
 */
ARSDK_API struct pomp_loop *arsdk_ctrl_get_loop(struct arsdk_ctrl *ctrl);

/**
 * Destroy controller.
 * @param ctrl : controller.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_ctrl_destroy(struct arsdk_ctrl *ctrl);

/**
 * Iterate through controller devices list.
 * @param ctrl : controller.
 * @param prev : previous device in list (NULL to start from the beginnin).
 * @return next device in list or NULL if no more device.
 */
ARSDK_API struct arsdk_device *arsdk_ctrl_next_device(struct arsdk_ctrl *ctrl,
		struct arsdk_device *prev);

/**
 * Get device from handle.
 * @param ctrl : controller.
 * @param handle : device handle.
 * @return device or NULL if not found.
 */
ARSDK_API struct arsdk_device *arsdk_ctrl_get_device(struct arsdk_ctrl *ctrl,
		uint16_t handle);


#endif /* !_ARSDK_CTRL_H_ */
