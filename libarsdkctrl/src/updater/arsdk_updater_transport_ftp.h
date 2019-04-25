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

#ifndef _ARSDK_UPDATER_TRANSPORT_FTP_H_
#define _ARSDK_UPDATER_TRANSPORT_FTP_H_

/** updater transport ftp */
struct arsdk_updater_transport_ftp;

/** updater transport ftp request */
struct arsdk_updater_ftp_req_upload;

/**
 * Create a updater ftp transport.
 * @param itf : updater interface.
 * @param ftp : ftp interface.
 * @param ret_obj : will receive the updater ftp transport.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_updater_transport_ftp_new(struct arsdk_updater_itf *itf,
		struct arsdk_ftp_itf *ftp,
		struct arsdk_updater_transport_ftp **ret_obj);

/**
 * Stop updater ftp transport.
 * @param tsprt : updater ftp transport.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_updater_transport_ftp_stop(struct arsdk_updater_transport_ftp *tsprt);

/**
 * Cancel all requests.
 * @param tsprt : updater ftp transport.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_updater_transport_ftp_cancel_all(
		struct arsdk_updater_transport_ftp *tsprt);

/**
 * Destroy updater ftp transport.
 * @param tsprt : updater ftp transport.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_updater_transport_ftp_destroy(
		struct arsdk_updater_transport_ftp *tsprt);

/**
 * Cancel a firmware "upload" request.
 * @param req : the request to cancel.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_updater_ftp_req_upload_cancel(
		struct arsdk_updater_ftp_req_upload *req);

/**
 * Create and send a updater firmware "upload" request.
 * @param tsprt : the updater ftp transport.
 * @param fw_filepath : firmware file to upload.
 * @param dev_type : type of the device to access.
 * @param cbs : request callback.
 * @param ret_req : will receive the request object.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_updater_transport_ftp_create_req_upload(
		struct arsdk_updater_transport_ftp *tsprt,
		const char *fw_filepath,
		enum arsdk_device_type dev_type,
		const struct arsdk_updater_req_upload_cbs *cbs,
		struct arsdk_updater_ftp_req_upload **ret_req);

/**
 * Get updater ftp transport parent.
 * @param req : updater request.
 * @return transport parent.
 */
struct arsdk_updater_transport *arsdk_updater_transport_ftp_get_parent(
		struct arsdk_updater_transport_ftp *self);

#endif /* !_ARSDK_UPDATER_TRANSPORT_FTP_H_ */
