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

#ifndef _ARSDK_FTP_H_
#define _ARSDK_FTP_H_

/* Net specific system headers */
#ifdef _WIN32
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0501
#  endif /* !_WIN32_WINNT */
#  define NOGDI
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#else /* !_WIN32 */
#  include <arpa/inet.h>
#endif /* !_WIN32 */

struct arsdk_ftp;
struct arsdk_ftp_req;

enum arsdk_ftp_status {
	ARSDK_FTP_STATUS_OK = 0,       /**< request succeeded */
	ARSDK_FTP_STATUS_CANCELED,     /**< request canceled by the user. */
	ARSDK_FTP_STATUS_FAILED,       /**< request failed */
	ARSDK_FTP_STATUS_ABORTED,      /**< request aborted by disconnection,*/
				       /**< no more request can be sent.*/
};

struct arsdk_ftp_req_cbs {
	/** User data given in callbacks */
	void *userdata;

	size_t (*read_data)(struct arsdk_ftp *itf,
			struct arsdk_ftp_req *req,
			void *ptr,
			size_t size,
			size_t nmemb,
			void *userdata);

	size_t (*write_data)(struct arsdk_ftp *ctx,
			struct arsdk_ftp_req *req,
			const void *ptr,
			size_t size,
			size_t nmemb,
			void *userdata);

	void (*progress)(struct arsdk_ftp *ctx,
			struct arsdk_ftp_req *req,
			double dltotal,
			double dlnow,
			float dlpercent,
			double ultotal,
			double ulnow,
			float ulpercent,
			void *userdata);

	void (*complete)(struct arsdk_ftp *ctx,
			struct arsdk_ftp_req *req,
			enum arsdk_ftp_status status,
			int error,
			void *userdata);
};

struct arsdk_ftp_cbs {
	void *userdata;

	void (*socketcb)(struct arsdk_ftp *ctx,
			int fd,
			enum arsdk_socket_kind kind,
			void *userdata);
};

/**
 * Create a ftp context.
 * @param loop :  event loop to use.
 * @param username : login user name.
 * @param password : login password.
 * @param cbs : ftp callbacks.
 * @param ret_ctx : will receive the ftp context.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_ftp_new(struct pomp_loop *loop,
		const char *username,
		const char *password,
		const struct arsdk_ftp_cbs *cbs,
		struct arsdk_ftp **ret_ctx);

/**
 * Destroy a ftp context.
 * @param ctx : ftp context.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_ftp_destroy(struct arsdk_ftp *ctx);

/**
 * Stop a ftp context.
 * @param ctx : ftp context.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_ftp_stop(struct arsdk_ftp *ctx);

/**
 * Cancel a ftp request.
 * @param ctx : ftp context.
 * @param req : ftp req.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_ftp_cancel_req(struct arsdk_ftp *ctx,
			 struct arsdk_ftp_req *req);

/**
 * Cancel all ftp requests pending.
 * @param ctx : ftp context.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_ftp_cancel_all(struct arsdk_ftp *ctx);

/**
 * Create and send "get" request.
 * @param ctx : ftp context.
 * @param cbs : ftp request callbacks.
 * @param url : url to download.
 * @param resume_off : resume offset.
 * @param ret_req : will receive the ftp request.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_ftp_get(struct arsdk_ftp *ctx,
		const struct arsdk_ftp_req_cbs *cbs,
		const char *url,
		int64_t resume_off,
		struct arsdk_ftp_req **ret_req);

/**
 * Create and send "put" request.
 * @param ctx : ftp context.
 * @param cbs : ftp request callbacks.
 * @param url : url where upload.
 * @param resume_off : resume offset.
 * @param in_size : size to upload.
 * @param ret_req : will receive the ftp request.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_ftp_put(struct arsdk_ftp *ctx,
		const struct arsdk_ftp_req_cbs *cbs,
		const char *url,
		int64_t resume_off,
		int64_t in_size,
		struct arsdk_ftp_req **ret_req);

/**
 * Create and send "size" request.
 * @param ctx : ftp context.
 * @param cbs : ftp request callbacks.
 * @param url : url where upload.
 * @param ret_req : will receive the updater ftp request.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_ftp_size(struct arsdk_ftp *ctx,
		const struct arsdk_ftp_req_cbs *cbs,
		const char *url,
		struct arsdk_ftp_req **ret_req);

/**
 * Create and send "rename" request.
 * @param ctx : ftp context.
 * @param cbs : ftp request callbacks.
 * @param url_src : url to rename.
 * @param dst : new name.
 * @param ret_req : will receive the ftp request.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_ftp_rename(struct arsdk_ftp *ctx,
		const struct arsdk_ftp_req_cbs *cbs,
		const char *url_src,
		const char *dst,
		struct arsdk_ftp_req **ret_req);

/**
 * Create and send "delete" request.
 * @param ctx : ftp context.
 * @param cbs : ftp request callbacks.
 * @param url : url to delete.
 * @param ret_req : will receive the ftp request.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_ftp_delete(struct arsdk_ftp *ctx,
		const struct arsdk_ftp_req_cbs *cbs,
		const char *url,
		struct arsdk_ftp_req **ret_req);

/**
 * Create and send "list" request.
 * @param ctx : ftp context.
 * @param cbs : ftp request callbacks.
 * @param url : url where list.
 * @param ret_req : will receive the ftp request.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_ftp_list(struct arsdk_ftp *ctx,
		const struct arsdk_ftp_req_cbs *cbs,
		const char *url,
		struct arsdk_ftp_req **ret_req);

/**
 * Get file size
 * @param req : the ftp request.
 * @return The size of the file subject of the request if known, otherwise 0.
 */
size_t arsdk_ftp_req_get_size(struct arsdk_ftp_req *req);

/**
 * Get request url
 * @param req : the ftp request.
 * @return The url of the request. NULL in error case.
 */
const char *arsdk_ftp_req_get_url(struct arsdk_ftp_req *req);

#endif /* !_ARSDK_FTP_H_ */
