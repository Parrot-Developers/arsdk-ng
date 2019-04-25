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

#ifndef _ARSDK_UPDATER_ITF_H_
#define _ARSDK_UPDATER_ITF_H_

struct arsdk_updater_transport;
struct arsdk_updater_req_upload;

/** updater request status */
enum arsdk_updater_req_status {
	/** request succeeded */
	ARSDK_UPDATER_REQ_STATUS_OK,
	/** request canceled by the user. */
	ARSDK_UPDATER_REQ_STATUS_CANCELED,
	/** request failed */
	ARSDK_UPDATER_REQ_STATUS_FAILED,
	/** request aborted by disconnection, no more request can be sent.*/
	ARSDK_UPDATER_REQ_STATUS_ABORTED,
};

/** firmware "upload" request callbacks */
struct arsdk_updater_req_upload_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Notify request progression.
	 * @param itf : the updater interface.
	 * @param req : the request.
	 * @param percent : progression percentage.
	 * @param userdata :  user data.
	 */
	void (*progress)(struct arsdk_updater_itf *itf,
			struct arsdk_updater_req_upload *req,
			float percent,
			void *userdata);

	/**
	 * Notify request completed.
	 * @param itf : the updater interface.
	 * @param req : the request.
	 * @param status : request status.
	 * @param error : request error.
	 * @param userdata : user data.
	 */
	void (*complete)(struct arsdk_updater_itf *itf,
			struct arsdk_updater_req_upload *req,
			enum arsdk_updater_req_status status,
			int error,
			void *userdata);
};

/**
 * Create and send a updater firmware "upload" request.
 * @param itf : the updater interface.
 * @param fw_filepath : firmware file to upload.
 * @param dev_type : type of the device to access.
 * @param cbs : request callback.
 * @param ret_req : will receive the request object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_updater_itf_create_req_upload(struct arsdk_updater_itf *itf,
		const char *fw_filepath,
		enum arsdk_device_type dev_type,
		const struct arsdk_updater_req_upload_cbs *cbs,
		struct arsdk_updater_req_upload **ret_req);

/**
 * Cancel a firmware "upload" request.
 * @param req : the request to cancel.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_updater_req_upload_cancel(
		struct arsdk_updater_req_upload *req);

/**
 * Get type of the device to update by the request.
 * @param req : request.
 * @return device type.
 */
ARSDK_API enum arsdk_device_type arsdk_updater_req_upload_get_dev_type(
		const struct arsdk_updater_req_upload *req);

/**
 * Cancel all requests.
 * @param itf : the updater interface.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_updater_itf_cancel_all(struct arsdk_updater_itf *itf);

/**
 * Get device type from firmware application id.
 * @param app_id : firmware application id.
 * @return Device type associated with this id.
 */
ARSDK_API enum arsdk_device_type arsdk_updater_appid_to_devtype(
		const uint32_t app_id);

#endif /* !_ARSDK_UPDATER_ITF_H_ */
