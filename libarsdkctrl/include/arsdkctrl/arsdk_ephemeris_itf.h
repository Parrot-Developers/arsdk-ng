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

#ifndef _ARSDK_EPHEMERIS_ITF_H_
#define _ARSDK_EPHEMERIS_ITF_H_

struct arsdk_ephemeris_req_upload;

/** updater request status */
enum arsdk_ephemeris_req_status {
	/** request succeeded */
	ARSDK_EPHEMERIS_REQ_STATUS_OK,
	/** request canceled by the user. */
	ARSDK_EPHEMERIS_REQ_STATUS_CANCELED,
	/** request failed */
	ARSDK_EPHEMERIS_REQ_STATUS_FAILED,
	/** request aborted by disconnection, no more request can be sent.*/
	ARSDK_EPHEMERIS_REQ_STATUS_ABORTED,
};

/** ephemeris "upload" request callbacks */
struct arsdk_ephemeris_req_upload_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Notify request progression.
	 * @param itf : the ephemeris interface.
	 * @param req : the request.
	 * @param percent : progression percentage.
	 * @param userdata :  user data.
	 */
	void (*progress)(struct arsdk_ephemeris_itf *itf,
			struct arsdk_ephemeris_req_upload *req,
			float percent,
			void *userdata);

	/**
	 * Notify request completed.
	 * @param itf : the ephemeris interface.
	 * @param req : the request.
	 * @param status : request status.
	 * @param error : request error.
	 * @param userdata : user data.
	 */
	void (*complete)(struct arsdk_ephemeris_itf *itf,
			struct arsdk_ephemeris_req_upload *req,
			enum arsdk_ephemeris_req_status status,
			int error,
			void *userdata);
};

/**
 * Create and send a ephemeris "upload" request.
 * @param itf : the ephemeris interface.
 * @param eph_filepath : ephemeris file to upload.
 * @param dev_type : type of the device to access.
 * @param cbs : request callback.
 * @param ret_req : will receive the request object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_ephemeris_itf_create_req_upload(
		struct arsdk_ephemeris_itf *itf,
		const char *eph_filepath,
		enum arsdk_device_type dev_type,
		const struct arsdk_ephemeris_req_upload_cbs *cbs,
		struct arsdk_ephemeris_req_upload **ret_req);

/**
 * Cancel a ephemeris "upload" request.
 * @param req : the request to cancel.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_ephemeris_req_upload_cancel(
		struct arsdk_ephemeris_req_upload *req);

/**
 * Get type of the device concerned by the request.
 * @param req : the request.
 * @return device type.
 */
ARSDK_API enum arsdk_device_type arsdk_ephemeris_req_upload_get_dev_type(
		const struct arsdk_ephemeris_req_upload *req);

/**
 * Get ephemeris local file path.
 * @param req : the request.
 * @return the path of the local ephemeris file.
 */
ARSDK_API const char *arsdk_ephemeris_req_upload_get_file_path(
		const struct arsdk_ephemeris_req_upload *req);

/**
 * Cancel all requests.
 * @param itf : the ephemeris interface.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_ephemeris_itf_cancel_all(struct arsdk_ephemeris_itf *itf);

#endif /* !_ARSDK_EPHEMERIS_ITF_H_ */
