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

#ifndef _ARSDK_MEDIA_ITF_H_
#define _ARSDK_MEDIA_ITF_H_

/** */
struct arsdk_media;
struct arsdk_media_res;
struct arsdk_media_list;
struct arsdk_media_req_list;
struct arsdk_media_req_download;
struct arsdk_media_req_delete;

/** */
enum arsdk_media_req_status {
	/** request succeeded */
	ARSDK_MEDIA_REQ_STATUS_OK,
	/** request canceled by the user. */
	ARSDK_MEDIA_REQ_STATUS_CANCELED,
	/** request failed */
	ARSDK_MEDIA_REQ_STATUS_FAILED,
	/** request aborted by disconnection, no more request can be sent.*/
	ARSDK_MEDIA_REQ_STATUS_ABORTED,
};

/** Types of media */
enum arsdk_media_type {
	ARSDK_MEDIA_TYPE_UNKNOWN = 0,        /**< Type Unknown */
	ARSDK_MEDIA_TYPE_PHOTO = (1 << 0),   /**< Photo */
	ARSDK_MEDIA_TYPE_VIDEO = (1 << 1),   /**< Video */
	/** All medias */
	ARSDK_MEDIA_TYPE_ALL = ARSDK_MEDIA_TYPE_PHOTO |
			       ARSDK_MEDIA_TYPE_VIDEO,
};

/** Types of media resource */
enum arsdk_media_res_type {
	ARSDK_MEDIA_RES_TYPE_UNKNOWN = -1,      /**< Type unknown */
	ARSDK_MEDIA_RES_TYPE_PHOTO,             /**< Photo */
	ARSDK_MEDIA_RES_TYPE_VIDEO,             /**< Video */
	ARSDK_MEDIA_RES_TYPE_THUMBNAIL,         /**< Thumbnail */
};

/** Format of media resource */
enum arsdk_media_res_format {
	ARSDK_MEDIA_RES_FMT_UNKNOWN = -1,       /**< Format unknown */
	ARSDK_MEDIA_RES_FMT_JPG,                /**< jpg */
	ARSDK_MEDIA_RES_FMT_DNG,                /**< dng */
	ARSDK_MEDIA_RES_FMT_MP4,                /**< mp4 */
};


/** "list" request callbacks */
struct arsdk_media_req_list_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Notify request completed.
	 * @param itf : the media interface.
	 * @param req : the request.
	 * @param status : request status.
	 * @param error : request error.
	 * @param userdata :  user data.
	 */
	void (*complete)(struct arsdk_media_itf *itf,
			struct arsdk_media_req_list *req,
			enum arsdk_media_req_status status,
			int error,
			void *userdata);
};

/**
 * Create and send a medias "list" request.
 * @param itf : the media interface.
 * @param cbs : request callback.
 * @param types : bit field of arsdk_media_type
 * @param dev_type : type of the device to access.
 * @param ret_req : will receive the request object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_media_itf_create_req_list(
		struct arsdk_media_itf *itf,
		const struct arsdk_media_req_list_cbs *cbs,
		uint32_t types,
		enum arsdk_device_type dev_type,
		struct arsdk_media_req_list **ret_req);

/**
 * Cancel a "list" request.
 * @param req : the request to cancel.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_media_req_list_cancel(struct arsdk_media_req_list *req);

/**
 * Get the type of the device intended by this "list" request.
 * @param req : the request.
 * @return the device intended by this request.
 */
ARSDK_API enum arsdk_device_type arsdk_media_req_list_get_dev_type(
		const struct arsdk_media_req_list *req);

/**
 * Get the result of the list request.
 * @param req : the request.
 * @return The media list, result of the "list" request.
 */
ARSDK_API struct arsdk_media_list *arsdk_media_req_list_get_result(
		struct arsdk_media_req_list *req);

/**
 * Iterate through medias list.
 * @param list : the media list.
 * @param prev : previous media in list (NULL to start from the beginning).
 * @return next media in list or NULL if no more media.
 */
ARSDK_API struct arsdk_media *arsdk_media_list_next_media(
		struct arsdk_media_list *list,
		struct arsdk_media *prev);

/**
 * Get number of media in the list.
 * @param list : the media list.
 * @return the number of media.
 */
ARSDK_API size_t arsdk_media_list_get_count(
		struct arsdk_media_list *list);

/**
 * Increase ref count of media list.
 * @param list : the media list.
 */
ARSDK_API void arsdk_media_list_ref(struct arsdk_media_list *list);

/**
 * Decrease ref count of media list. When it reaches 0 media is freed.
 * @param list : the media list.
 */
ARSDK_API void arsdk_media_list_unref(struct arsdk_media_list *list);


/** "download" request callbacks */
struct arsdk_media_req_download_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Notify request progression.
	 * @param itf : the media interface.
	 * @param req : the request.
	 * @param percent : progression percentage.
	 * @param userdata :  user data.
	 */
	void (*progress)(struct arsdk_media_itf *itf,
			struct arsdk_media_req_download *req,
			float percent,
			void *userdata);

	/**
	 * Notify request completed.
	 * @param itf : the media interface.
	 * @param req : the request.
	 * @param status : request status.
	 * @param error : request error.
	 * @param userdata :  user data.
	 */
	void (*complete)(struct arsdk_media_itf *itf,
			struct arsdk_media_req_download *req,
			enum arsdk_media_req_status status,
			int error,
			void *userdata);
};

/**
 * Create and send a media "download" request.
 * @param itf : the media interface.
 * @param cbs : request callback.
 * @param res_uri : the uri of the media resource to download.
 * @param local_path : local path where download.
 * If local_path is NULL, a buffer will be use as output.
 * This buffer can be got by 'arsdk_media_download_res_get_buffer()'.
 * @param dev_type : type of the device to access.
 * @param is_resume : "1" to notify to resume old "download" request,
 *                    otherwise "0".
 * @param ret_req : will receive the request object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_media_itf_create_req_download(
		struct arsdk_media_itf *itf,
		const struct arsdk_media_req_download_cbs *cbs,
		const char *res_uri,
		const char *local_path,
		enum arsdk_device_type dev_type,
		uint8_t is_resume,
		struct arsdk_media_req_download **ret_req);

/**
 * Cancel a "download" request.
 * @param req : the request to cancel.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_media_req_download_cancel(
		struct arsdk_media_req_download *req);

/**
 * Get the uri of the "download" request.
 * @param req : the request.
 * @return the uri to download.
 */
ARSDK_API const char *arsdk_media_req_download_get_uri(
		const struct arsdk_media_req_download *req);

/**
 * Get the local path of "download" request.
 * @param req : the request.
 * @return the local path where download or null if local path is not set.
 */
ARSDK_API const char *arsdk_media_req_download_get_local_path(
		const struct arsdk_media_req_download *req);

/**
 * Get the output buffer of "download" request.
 * @param req : the request.
 * @return the output buffer if local path is not set otherwise NULL.
 */
ARSDK_API struct pomp_buffer *arsdk_media_req_download_get_buffer(
		const struct arsdk_media_req_download *req);

/**
 * Get the type of the device intended by this "download" request.
 * @param req : the request.
 * @return the device intended by this request.
 */
ARSDK_API enum arsdk_device_type arsdk_media_req_download_get_dev_type(
		const struct arsdk_media_req_download *req);

/** "delete" request callbacks */
struct arsdk_media_req_delete_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Notify request completed.
	 * @param itf : the media interface.
	 * @param req : the request.
	 * @param status : request status.
	 * @param error : request error.
	 * @param userdata :  user data.
	 */
	void (*complete)(struct arsdk_media_itf *itf,
			struct arsdk_media_req_delete *req,
			enum arsdk_media_req_status status,
			int error,
			void *userdata);
};

/**
 * Create and send "delete" request.
 * @param itf : the media interface.
 * @param cbs : request callback.
 * @param media : media to delete.
 * @param dev_type : type of the device to access.
 * @param ret_req : will receive the request object.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_media_itf_create_req_delete(
		struct arsdk_media_itf *itf,
		const struct arsdk_media_req_delete_cbs *cbs,
		struct arsdk_media *media,
		enum arsdk_device_type dev_type,
		struct arsdk_media_req_delete **ret_req);

/**
 * Get the media of "delete" request.
 * @param media : the media to delete.
 * @return the id of the media to delete.
 */
ARSDK_API const struct arsdk_media *
arsdk_media_req_delete_get_media(
		const struct arsdk_media_req_delete *req);

/**
 * Get the type of the device intended by this "delete" request.
 * @param req : the request.
 * @return the device intended by this request.
 */
ARSDK_API enum arsdk_device_type arsdk_media_req_delete_get_dev_type(
		const struct arsdk_media_req_delete *req);

/**
 * Cancel all requests pending or in progress.
 * @param itf : the media interface.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_media_itf_cancel_all(struct arsdk_media_itf *itf);

/**
 * Get the name of a media.
 * @param media : the media.
 * @return media name.
 */
ARSDK_API const char *arsdk_media_get_name(const struct arsdk_media *media);

/**
 * Get the run id of a media.
 * @param media : the media.
 * @return media run id.
 */
ARSDK_API const char *arsdk_media_get_runid(const struct arsdk_media *media);

/**
 * Get the date of a media.
 * @param media : the media.
 * @return media date.
 */
ARSDK_API const struct tm *arsdk_media_get_date(
		const struct arsdk_media *media);

/**
 * Get the type of a media.
 * @param media : the media.
 * @return the media type.
 */
ARSDK_API enum arsdk_media_type arsdk_media_get_type(
		const struct arsdk_media *media);

/**
 * Iterate through resource list of a media.
 * @param media : the media.
 * @param prev : previous resource in list (NULL to start from the beginning).
 * @return next resource in list or NULL if no more resource.
 */
ARSDK_API struct arsdk_media_res *arsdk_media_next_res(
		struct arsdk_media *media,
		struct arsdk_media_res *prev);

/**
 * Get the number of resource of a media.
 * @param media : the media.
 * @return number of resource.
 */
ARSDK_API size_t arsdk_media_get_res_count(
		const struct arsdk_media *media);

/**
 * Increase ref count of  media resource.
 * @param media : the media.
 */
ARSDK_API void arsdk_media_ref(struct arsdk_media *media);

/**
 * Decrease ref count of media. When it reaches 0 media is freed.
 * @param media : the media.
 */
ARSDK_API void arsdk_media_unref(struct arsdk_media *media);

/**
 * Get the type of a resource .
 * @param resource : the resource.
 * @return the resource type.
 */
ARSDK_API enum arsdk_media_res_type arsdk_media_res_get_type(
		const struct arsdk_media_res *resource);

/**
 * Get the format of a resource .
 * @param resource : the resource.
 * @return the resource format.
 */
ARSDK_API enum arsdk_media_res_format arsdk_media_res_get_fmt(
		const struct arsdk_media_res *resource);

/**
 * Get the uri of a resource .
 * @param resource : the resource.
 * @return resource uri.
 */
ARSDK_API const char *arsdk_media_res_get_uri(
		const struct arsdk_media_res *resource);

/**
 * Get the size of a resource.
 * @param resource : the resource.
 * @return resource size.
 * @remark Size of thumbnail resources is not set, always 0.
 */
ARSDK_API size_t arsdk_media_res_get_size(
		const struct arsdk_media_res *resource);

/**
 * Get resource name.
 * @param file : the resource.
 * @return resource name.
 */
ARSDK_API const char *arsdk_media_res_get_name(
		const struct arsdk_media_res *resource);

#endif /* !_ARSDK_MEDIA_ITF_H_ */
