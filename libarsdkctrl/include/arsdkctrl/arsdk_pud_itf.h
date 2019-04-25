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

#ifndef _ARSDK_PUD_ITF_H_
#define _ARSDK_PUD_ITF_H_

struct arsdk_pud_req;

/** pud interface status */
enum arsdk_pud_req_status {
	ARSDK_PUD_REQ_STATUS_OK,        /**< request succeeded */
	ARSDK_PUD_REQ_STATUS_CANCELED,  /**< request canceled by the user. */
	ARSDK_PUD_REQ_STATUS_FAILED,    /**< request failed */
	ARSDK_PUD_REQ_STATUS_ABORTED,   /**< request aborted by disconnection,*/
					/**< no more request can be sent.*/
};

/** pud callbacks */
struct arsdk_pud_req_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Notify pud progression.
	 * One pud download is complete.
	 * @param itf : the pud interface.
	 * @param path : pud local path.
	 * @param count : download count.
	 * @param total : total number of pud to download.
	 * @param status : download status.
	 * @param userdata : user data.
	 */
	void (*progress)(struct arsdk_pud_itf *itf,
			struct arsdk_pud_req *req,
			const char *path,
			int count,
			int total,
			enum arsdk_pud_req_status status,
			void *userdata);

	/**
	 * Notify pud completed.
	 * @param itf : the pud interface.
	 * @param status : status.
	 * @param error : error.
	 * @param userdata : user data.
	 */
	void (*complete)(struct arsdk_pud_itf *itf,
			struct arsdk_pud_req *req,
			enum arsdk_pud_req_status status,
			int error,
			void *userdata);
};

/**
 * Cancel all requests.
 * @param itf : the pud interface to download run data from the device.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_pud_itf_cancel_all(struct arsdk_pud_itf *itf);

/**
 * Start to download run data from the remote device.
 * @param itf : the pud interface.
 * @param local_path : the local directory where download.
 * @param dev_type : type of the device to access.
 * @param cbs : callback.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_pud_itf_create_req(struct arsdk_pud_itf *itf,
		const char *local_path,
		enum arsdk_device_type dev_type,
		const struct arsdk_pud_req_cbs *cbs,
		struct arsdk_pud_req **ret_req);

/**
 * Cancel the pud request.
 * @param req : the pud request.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_pud_req_cancel(struct arsdk_pud_req *req);

/**
 * Get the type of the device intended by this request.
 * @param req : the pud request.
 * @return device type.
 */
ARSDK_API enum arsdk_device_type arsdk_pud_req_get_dev_type(
		const struct arsdk_pud_req *req);

#endif /* !_ARSDK_PUD_ITF_H_ */
