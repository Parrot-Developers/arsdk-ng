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

#ifndef _ARSDK_UPDATER_ITF_PRIV_H_
#define _ARSDK_UPDATER_ITF_PRIV_H_

#ifdef BUILD_LIBPUF
#  include <libpuf.h>
#endif /* !BUILD_LIBPUF */

/**
 * Create a updater interface.
 * @param dev_info : device information.
 * @param ftp_itf : ftp interface.
 * @param mux_ctx : mux context.
 * @param ret_itf : will receive the updater interface.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_updater_itf_new(struct arsdk_device_info *dev_info,
		struct arsdk_ftp_itf *ftp_itf,
		struct mux_ctx *mux,
		struct arsdk_updater_itf **ret_itf);

/**
 * Destroy updater interface.
 * @param itf : updater interface.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_updater_itf_destroy(struct arsdk_updater_itf *itf);

/**
 * Stop updater interface.
 * @param itf : updater interface.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_updater_itf_stop(struct arsdk_updater_itf *itf);

/**
 * Create a updater firmware "upload" request.
 * @param child : child.
 * @param cbs : request callback.
 * @param dev_type : type of the device to access.
 * @param ret_req : will receive the updater request.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_updater_new_req_upload(
		struct arsdk_updater_transport *tsprt,
		void *child,
		const struct arsdk_updater_req_upload_cbs *cbs,
		enum arsdk_device_type dev_type,
		struct arsdk_updater_req_upload **ret_req);

/**
 * Destroy updater request.
 * @param req : updater request.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_updater_destroy_req_upload(
		struct arsdk_updater_req_upload *req);

/**
 * Get request child.
 * @param req : updater request.
 * @return updater request child.
 */
void *arsdk_updater_req_upload_child(struct arsdk_updater_req_upload *req);

/**
 * Get updater transport from request.
 * @param req : request.
 * @return updater transport.
 */
struct arsdk_updater_transport *arsdk_updater_req_upload_get_transport(
		struct arsdk_updater_req_upload *req);

#define ARSDK_UPDATER_NAME_LENGTH 50

/**
 * Firmware information
 */
struct arsdk_updater_fw_info {
#ifdef BUILD_LIBPUF
	struct puf_version                      version;
#endif /* BUILD_LIBPUF */
	char                                    name[ARSDK_UPDATER_NAME_LENGTH];
	enum arsdk_device_type                  devtype;
	size_t                                  size;
	uint8_t                                 md5[ARSDK_MD5_LENGTH];
};

/**
 * Read firmware information from file
 * @param fw_filepath : firmware file path.
 * @param info : information of the firmware.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_updater_read_fw_info(const char *fw_filepath,
		struct arsdk_updater_fw_info *info);

/**
 * Check if a firmware is compliant with a device.
 * @param info : information of the firmware.
 * @param dev_type : type of the device.
 * @return 1 if the firmware and the device are compliant, otherwise 0.
 */
int arsdk_updater_fw_dev_comp(struct arsdk_updater_fw_info *info,
		enum arsdk_device_type dev_type);

#endif /* !_ARSDK_UPDATER_ITF_PRIV_H_ */

