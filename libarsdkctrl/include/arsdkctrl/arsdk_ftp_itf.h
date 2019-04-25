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

#ifndef _ARSDK_FTP_ITF_H_
#define _ARSDK_FTP_ITF_H_

struct arsdk_ftp_file_list;
struct arsdk_ftp_req_put;
struct arsdk_ftp_req_get;
struct arsdk_ftp_req_rename;
struct arsdk_ftp_req_delete;
struct arsdk_ftp_req_list;

/** */
struct arsdk_ftp_file;

/** Ftp Servers */
enum arsdk_ftp_srv_type {
	ARSDK_FTP_SRV_TYPE_UNKNOWN = -1,    /**< Server unknown */
	ARSDK_FTP_SRV_TYPE_MEDIA,           /**< Media ftp server */
	ARSDK_FTP_SRV_TYPE_UPDATE,          /**< Update ftp server */
	ARSDK_FTP_SRV_TYPE_FLIGHT_PLAN,     /**< Flight Plan ftp server */
};

/** Request status */
enum arsdk_ftp_req_status {
	ARSDK_FTP_REQ_STATUS_OK,        /**< request succeeded */
	ARSDK_FTP_REQ_STATUS_CANCELED,  /**< request canceled by the user. */
	ARSDK_FTP_REQ_STATUS_FAILED,    /**< request failed */
	ARSDK_FTP_REQ_STATUS_ABORTED,   /**< request aborted by disconnection,*/
					/**< no more request can be sent.*/
};

/** Types of file */
enum arsdk_ftp_file_type {
	ARSDK_FTP_FILE_TYPE_UNKNOWN = -1,       /**< Type unknown */
	ARSDK_FTP_FILE_TYPE_FILE,               /**< File */
	ARSDK_FTP_FILE_TYPE_DIR,                /**< Directory */
	ARSDK_FTP_FILE_TYPE_LINK,               /**< Symbolic Link */
};

/** "put" request callbacks */
struct arsdk_ftp_req_put_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Notify request progression.
	 * @param itf : the ftp interface.
	 * @param req : the request.
	 * @param percent : progression percentage.
	 * @param userdata :  user data.
	 */
	void (*progress)(struct arsdk_ftp_itf *itf,
			struct arsdk_ftp_req_put *req,
			float percent,
			void *userdata);

	/**
	 * Notify request completed.
	 * @param itf : the ftp interface.
	 * @param req : the request.
	 * @param status : request status.
	 * @param error : request error.
	 * @param userdata :  user data.
	 */
	void (*complete)(struct arsdk_ftp_itf *itf,
			struct arsdk_ftp_req_put *req,
			enum arsdk_ftp_req_status status,
			int error,
			void *userdata);
};

/** "get" request callbacks */
struct arsdk_ftp_req_get_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Notify request progression.
	 * @param itf : the ftp interface.
	 * @param req : the request.
	 * @param percent : progression percentage.
	 * @param userdata :  user data.
	 */
	void (*progress)(struct arsdk_ftp_itf *itf,
			struct arsdk_ftp_req_get *req,
			float percent,
			void *userdata);

	/**
	 * Notify request completed.
	 * @param itf : the ftp interface.
	 * @param req : the request.
	 * @param status : request status.
	 * @param error : request error.
	 * @param userdata :  user data.
	 */
	void (*complete)(struct arsdk_ftp_itf *itf,
			struct arsdk_ftp_req_get *req,
			enum arsdk_ftp_req_status status,
			int error,
			void *userdata);
};

/** "rename" request callbacks */
struct arsdk_ftp_req_rename_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Notify request completed.
	 * @param itf : the ftp interface.
	 * @param req : the request.
	 * @param status : request status.
	 * @param error : request error.
	 * @param userdata :  user data.
	 */
	void (*complete)(struct arsdk_ftp_itf *itf,
			struct arsdk_ftp_req_rename *req,
			enum arsdk_ftp_req_status status,
			int error,
			void *userdata);
};

/** "delete" request callbacks */
struct arsdk_ftp_req_delete_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Notify request completed.
	 * @param itf : the ftp interface.
	 * @param req : the request.
	 * @param status : request status.
	 * @param error : request error.
	 * @param userdata :  user data.
	 */
	void (*complete)(struct arsdk_ftp_itf *itf,
			struct arsdk_ftp_req_delete *req,
			enum arsdk_ftp_req_status status,
			int error,
			void *userdata);
};

/** "list" request callbacks */
struct arsdk_ftp_req_list_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Notify request completed.
	 * @param itf : the ftp interface.
	 * @param req : the request.
	 * @param status : request status.
	 * @param error : request error.
	 * @param userdata :  user data.
	 */
	void (*complete)(struct arsdk_ftp_itf *itf,
			struct arsdk_ftp_req_list *req,
			enum arsdk_ftp_req_status status,
			int error,
			void *userdata);
};

/**
 * Cancel all requests pending or in progress.
 * @param itf : the ftp interface.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_ftp_itf_cancel_all(
		struct arsdk_ftp_itf *itf);

/**
 * Create and send a ftp "get" request.
 * @param itf : the ftp interface.
 * @param cbs : request callback.
 * @param dev_type : type of the device to access.
 * @param srv : ftp server to access.
 * @param remote_path : the remote path to get.
 * @param local_path : the local path where copy.
 * If local_path is NULL, a buffer will be use as output.
 * This buffer can be got by 'arsdk_ftp_itf_req_get_get_buffer()'.
 * @param is_resume : "1" to notify to resume old "get" request, otherwise "0".
 * @param ret_req : will receive the request object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_ftp_itf_create_req_get(
		struct arsdk_ftp_itf *itf,
		const struct arsdk_ftp_req_get_cbs *cbs,
		enum arsdk_device_type dev_type,
		enum arsdk_ftp_srv_type srv_type,
		const char *remote_path,
		const char *local_path,
		uint8_t is_resume,
		struct arsdk_ftp_req_get **ret_req);

/**
 * Cancel a "get" request.
 * @param req : the request to cancel.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_ftp_req_get_cancel(struct arsdk_ftp_req_get *req);

/**
 * Get the remote path of a ftp "get" request.
 * @param req : the request.
 * @return the remote path.
 */
ARSDK_API const char *arsdk_ftp_req_get_get_remote_path(
		const struct arsdk_ftp_req_get *req);

/**
 * Get the local path of a ftp "get" request.
 * @param req : the request.
 * @return the local path or null if local path is  not set.
 */
ARSDK_API const char *arsdk_ftp_req_get_get_local_path(
		const struct arsdk_ftp_req_get *req);

/**
 * Get the output buffer of a ftp "get" request.
 * @param req : the request.
 * @return the output buffer if local path is not set otherwise NULL.
 */
ARSDK_API struct pomp_buffer *arsdk_ftp_req_get_get_buffer(
		const struct arsdk_ftp_req_get *req);

/**
 * Get the type of the device intended by this a ftp "get" request.
 * @param req : the request.
 * @return the device intended by this request.
 */
ARSDK_API enum arsdk_device_type arsdk_ftp_req_get_get_dev_type(
		const struct arsdk_ftp_req_get *req);

/**
 * Get the total size to "get".
 * @param req : the request.
 * @return the total size.
 */
ARSDK_API size_t arsdk_ftp_req_get_get_total_size(
		const struct arsdk_ftp_req_get *req);

/**
 * Get the downloaded size.
 * @param req : the request.
 * @return the downloaded size.
 */
ARSDK_API size_t arsdk_ftp_req_get_get_dlsize(
		const struct arsdk_ftp_req_get *req);

/**
 * Create and send a ftp "put" request.
 * @param itf : the ftp interface.
 * @param cbs : request callback.
 * @param dev_type : type of the device to access.
 * @param srv : ftp server to access.
 * @param remote_path : the remote path where copy.
 * @param local_path : the local path to put.
 * @param is_resume : "1" to notify to resume old "put" request, otherwise "0".
 * @param ret_req : will receive the request object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_ftp_itf_create_req_put(
		struct arsdk_ftp_itf *itf,
		const struct arsdk_ftp_req_put_cbs *cbs,
		enum arsdk_device_type dev_type,
		enum arsdk_ftp_srv_type srv_type,
		const char *remote_path,
		const char *local_path,
		uint8_t is_resume,
		struct arsdk_ftp_req_put **ret_req);

/**
 * Create and send a ftp "put" request.
 * @param itf : the ftp interface.
 * @param cbs : request callback.
 * @param dev_type : type of the device to access.
 * @param srv : ftp server to access.
 * @param remote_path : the remote path where copy.
 * @param buffer : the buffer to put.
 * @param is_resume : "1" to notify to resume old "put" request, otherwise "0".
 * @param ret_req : will receive the request object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_ftp_itf_create_req_put_buff(
		struct arsdk_ftp_itf *itf,
		const struct arsdk_ftp_req_put_cbs *cbs,
		enum arsdk_device_type dev_type,
		enum arsdk_ftp_srv_type srv_type,
		const char *remote_path,
		struct pomp_buffer *buffer,
		uint8_t is_resume,
		struct arsdk_ftp_req_put **ret_req);

/**
 * Cancel a "put" request.
 * @param req : the request to cancel.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_ftp_req_put_cancel(struct arsdk_ftp_req_put *req);

/**
 * Get the remote path of a ftp "put" request.
 * @param req : the request.
 * @return the remote path.
 */
ARSDK_API const char *arsdk_ftp_req_put_get_remote_path(
		const struct arsdk_ftp_req_put *req);

/**
 * Get the local path of a ftp "put" request.
 * @param req : the request.
 * @return the local path.
 */
ARSDK_API const char *arsdk_ftp_req_put_get_local_path(
		const struct arsdk_ftp_req_put *req);

/**
 * Get the type of the device intended by this a ftp "put" request.
 * @param req : the request.
 * @return the device intended by this request.
 */
ARSDK_API enum arsdk_device_type arsdk_ftp_req_put_get_dev_type(
		const struct arsdk_ftp_req_put *req);

/**
 * Get the total size to "put".
 * @param req : the request.
 * @return the total size.
 */
ARSDK_API size_t arsdk_ftp_req_put_get_total_size(
		const struct arsdk_ftp_req_put *req);

/**
 * Get the uploaded size.
 * @param req : the request.
 * @return the uploaded size.
 */
ARSDK_API size_t arsdk_ftp_req_put_get_ulsize(
		const struct arsdk_ftp_req_put *req);

/**
 * Create and send a ftp "rename" request.
 * @param itf : the ftp interface.
 * @param cbs : request callback.
 * @param dev_type : type of the device to access.
 * @param srv : ftp server to access.
 * @param src : the remote path to rename.
 * @param dst : the destination of the rename.
 * @param ret_req : will receive the request object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_ftp_itf_create_req_rename(
		struct arsdk_ftp_itf *itf,
		const struct arsdk_ftp_req_rename_cbs *cbs,
		enum arsdk_device_type dev_type,
		enum arsdk_ftp_srv_type srv_type,
		const char *src,
		const char *dst,
		struct arsdk_ftp_req_rename **ret_req);

/**
 * Cancel a "rename" request.
 * @param req : the request to cancel.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_ftp_req_rename_cancel(struct arsdk_ftp_req_rename *req);

/**
 * Get the source of a ftp "rename" request.
 * @param req : the request.
 * @return the source.
 */
ARSDK_API const char *arsdk_ftp_req_rename_get_src(
		const struct arsdk_ftp_req_rename *req);

/**
 * Get the destination of a ftp "rename" request.
 * @param req : the request.
 * @return the destination.
 */
ARSDK_API const char *arsdk_ftp_req_rename_get_dst(
		const struct arsdk_ftp_req_rename *req);

/**
 * Get the type of the device intended by this a ftp "rename" request.
 * @param req : the request.
 * @return the device intended by this request.
 */
ARSDK_API enum arsdk_device_type arsdk_ftp_req_rename_get_dev_type(
		const struct arsdk_ftp_req_rename *req);

/**
 * Create and send a ftp "delete" request.
 * @param itf : the ftp interface.
 * @param cbs : request callback.
 * @param dev_type : type of the device to access.
 * @param srv : ftp server to access.
 * @param remote_path : the remote path to delete.
 * @param ret_req : will receive the request object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_ftp_itf_create_req_delete(
		struct arsdk_ftp_itf *itf,
		const struct arsdk_ftp_req_delete_cbs *cbs,
		enum arsdk_device_type dev_type,
		enum arsdk_ftp_srv_type srv_type,
		const char *remote_path,
		struct arsdk_ftp_req_delete **ret_req);

/**
 * Cancel a "delete" request.
 * @param req : the request to cancel.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_ftp_req_delete_cancel(struct arsdk_ftp_req_delete *req);

/**
 * Get the path of a ftp "delete" request.
 * @param req : the request.
 * @return the path.
 */
ARSDK_API const char *arsdk_ftp_req_delete_get_path(
		const struct arsdk_ftp_req_delete *req);

/**
 * Get the type of the device intended by this a ftp "delete" request.
 * @param req : the request.
 * @return the device intended by this request.
 */
ARSDK_API enum arsdk_device_type arsdk_ftp_req_delete_get_dev_type(
		const struct arsdk_ftp_req_delete *req);

/**
 * Create and send a ftp "list" request.
 * @param itf : the ftp interface.
 * @param cbs : request callback.
 * @param dev_type : type of the device to access.
 * @param srv : ftp server to access.
 * @param remote_path : the remote path to list.
 * @param ret_req : will receive the request object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_ftp_itf_create_req_list(
		struct arsdk_ftp_itf *itf,
		const struct arsdk_ftp_req_list_cbs *cbs,
		enum arsdk_device_type dev_type,
		enum arsdk_ftp_srv_type srv_type,
		const char *remote_path,
		struct arsdk_ftp_req_list **ret_req);

/**
 * Cancel a "list" request.
 * @param req : the request to cancel.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_ftp_req_list_cancel(struct arsdk_ftp_req_list *req);

/**
 * Get the path of a ftp "list" request.
 * @param req : the request.
 * @return the path.
 */
ARSDK_API const char *arsdk_ftp_req_list_get_path(
		const struct arsdk_ftp_req_list *req);

/**
 * Get the type of the device intended by this a ftp "list" request.
 * @param req : the request.
 * @return the device intended by this request.
 */
ARSDK_API enum arsdk_device_type arsdk_ftp_req_list_get_dev_type(
		const struct arsdk_ftp_req_list *req);

/**
 * Get the result of the "list" request.
 * @param req : the request.
 * @return The file list, result of the "list" request.
 */
ARSDK_API struct arsdk_ftp_file_list *arsdk_ftp_req_list_get_result(
		struct arsdk_ftp_req_list *req);

/*
 * File API :
 */

/**
 * Iterate through file list.
 * @param list : the file list.
 * @param prev : previous file in list (NULL to start from the beginning).
 * @return next file in list or NULL if no more file.
 */
ARSDK_API struct arsdk_ftp_file *arsdk_ftp_file_list_next_file(
		struct arsdk_ftp_file_list *list,
		struct arsdk_ftp_file *prev);

/**
 * Get number of media in the list.
 * @param list : the file list.
 * @return the number of file.
 */
ARSDK_API size_t arsdk_ftp_file_list_get_count(
		struct arsdk_ftp_file_list *list);

/**
 * Increase ref count of file list.
 * @param list : the file list.
 */
ARSDK_API void arsdk_ftp_file_list_ref(struct arsdk_ftp_file_list *list);

/**
 * Decrease ref count of media list. When it reaches 0 media is freed.
 * @param list : the file list.
 */
ARSDK_API void arsdk_ftp_file_list_unref(struct arsdk_ftp_file_list *list);

/**
 * Get file name.
 * @param file : the file.
 * @return file name.
 */
ARSDK_API const char *arsdk_ftp_file_get_name(
		const struct arsdk_ftp_file *file);

/**
 * Get file size.
 * @param file : the file.
 * @return file size.
 */
ARSDK_API size_t arsdk_ftp_file_get_size(
		const struct arsdk_ftp_file *file);

/**
 * Get file type.
 * @param file : the file.
 * @return the file type.
 */
ARSDK_API enum arsdk_ftp_file_type arsdk_ftp_file_get_type(
		const struct arsdk_ftp_file *file);

/**
 * Increase ref count of file.
 * @param file : the file.
 */
ARSDK_API void arsdk_ftp_file_ref(struct arsdk_ftp_file *file);

/**
 * Decrease ref count of file. When it reaches 0 file is freed.
 * @param file : the file.
 */
ARSDK_API void arsdk_ftp_file_unref(struct arsdk_ftp_file *file);

#endif /* !_ARSDK_FTP_ITF_H_ */
