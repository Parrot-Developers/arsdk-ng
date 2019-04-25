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

#ifndef _ARSDK_CRASHML_ITF_H_
#define _ARSDK_CRASHML_ITF_H_

struct arsdk_crashml_req;

/** crashml types */
enum arsdk_crashml_type {
	ARSDK_CRASHML_TYPE_DIR = (1 << 0),     /**< Directory type */
	ARSDK_CRASHML_TYPE_TARGZ = (1 << 1),   /**< .tar.gz type */
};

/** crashml interface status */
enum arsdk_crashml_req_status {
	/** request succeeded */
	ARSDK_CRASHML_REQ_STATUS_OK,
	/** request canceled by the user. */
	ARSDK_CRASHML_REQ_STATUS_CANCELED,
	/** request failed */
	ARSDK_CRASHML_REQ_STATUS_FAILED,
	/** request aborted by disconnection, no more request can be sent.*/
	ARSDK_CRASHML_REQ_STATUS_ABORTED,
};

/** crashml callbacks */
struct arsdk_crashml_req_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Notify crashml progression.
	 * One crashml download is complete.
	 * @param itf : the crashml interface.
	 * @param path : crashml local path.
	 * @param count : download count.
	 * @param total : total number of crashml to download.
	 * @param status : download status.
	 * @param userdata : user data.
	 */
	void (*progress)(struct arsdk_crashml_itf *itf,
			struct arsdk_crashml_req *req,
			const char *path,
			int count,
			int total,
			enum arsdk_crashml_req_status status,
			void *userdata);

	/**
	 * Notify crashml completed.
	 * @param itf : the crashml interface.
	 * @param status : status.
	 * @param error : error.
	 * @param userdata : user data.
	 */
	void (*complete)(struct arsdk_crashml_itf *itf,
			struct arsdk_crashml_req *req,
			enum arsdk_crashml_req_status status,
			int error,
			void *userdata);
};

/**
 * Cancel all requests.
 * @param itf : the crashml interface.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_crashml_itf_cancel_all(struct arsdk_crashml_itf *itf);

/**
 * Start to download crashmls from the remote device.
 * @param itf : the crashml interface.
 * @param local_path : the local directory where download.
 * @param dev_type : type of the device to access.
 * @param cbs : callback.
 * @param crashml_types: bit field of arsdk_crashml_type to download.
 * @param ret_req: will receive the request object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_crashml_itf_create_req(struct arsdk_crashml_itf *itf,
		const char *local_path,
		enum arsdk_device_type dev_type,
		const struct arsdk_crashml_req_cbs *cbs,
		uint32_t crashml_types,
		struct arsdk_crashml_req **ret_req);

/**
 * Cancel the crashml request.
 * @param req : the crashml request.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_crashml_req_cancel(struct arsdk_crashml_req *req);

/**
 * Get the type of the device intended by this request.
 * @param req : the crashml request.
 * @return device type.
 */
ARSDK_API enum arsdk_device_type arsdk_crashml_req_get_dev_type(
		const struct arsdk_crashml_req *req);

#endif /* !_ARSDK_CRASHML_ITF_H_ */
