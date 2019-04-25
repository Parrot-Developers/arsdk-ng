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

#ifndef _ARSDK_LOG_H_
#define _ARSDK_LOG_H_

#ifndef ULOG_TAG
#error "No log tag defined"
#endif

#if defined(BUILD_LIBULOG)
#include "ulog.h"

/** Log as debug */
#define ARSDK_LOGD(_fmt, ...)	ULOGD(_fmt, ##__VA_ARGS__)
/** Log as info */
#define ARSDK_LOGI(_fmt, ...)	ULOGI(_fmt, ##__VA_ARGS__)
/** Log as warning */
#define ARSDK_LOGW(_fmt, ...)	ULOGW(_fmt, ##__VA_ARGS__)
/** Log as error */
#define ARSDK_LOGE(_fmt, ...)	ULOGE(_fmt, ##__VA_ARGS__)

/** Log without truncation as info */
#define ARSDK_LOGI_STR(_str)    ULOG_STR(ULOG_INFO, _str)

/** Log with va_list */
#define ARSDK_LOG_PRI_VA(_prio, _fmt, _a)   ULOG_PRI_VA(_prio, _fmt, _a)

#else /* !BUILD_LIBULOG */

#define ARSDK_STRINGIFY(x) #x
#define ARSDK_STR(x) ARSDK_STRINGIFY(x)

/** Generic log */
#define ARSDK_LOG(_fmt, ...)	fprintf(stderr, ARSDK_STRINGIFY(ULOG_TAG) \
				"\t"_fmt "\n", ##__VA_ARGS__)
/** Log as debug */
#define ARSDK_LOGD(_fmt, ...)	ARSDK_LOG("[D] " _fmt, ##__VA_ARGS__)
/** Log as info */
#define ARSDK_LOGI(_fmt, ...)	ARSDK_LOG("[I] " _fmt, ##__VA_ARGS__)
/** Log as warning */
#define ARSDK_LOGW(_fmt, ...)	ARSDK_LOG("[W] " _fmt, ##__VA_ARGS__)
/** Log as error */
#define ARSDK_LOGE(_fmt, ...)	ARSDK_LOG("[E] " _fmt, ##__VA_ARGS__)

/** Log without truncation as info */
#define ARSDK_LOGI_STR(_str)    ARSDK_LOG("[I] %s", _str)

/** Log with va_list */
#define ARSDK_LOG_PRI_VA(_prio, _fmt, _a) vfprintf(stderr,\
		ARSDK_STRINGIFY(ULOG_TAG) "\t"_fmt "\n", _a)

#endif /* !BUILD_LIBULOG */

#ifdef _MSC_VER
#  ifndef __cplusplus
/* codecheck_ignore[USE_FUNC] */
#    define __func__  __FUNCTION__
#  endif /* __cplusplus */
#endif /* _MSC_VER */

/** Log error with errno */
#define ARSDK_LOG_ERRNO(_fct, _err) \
	ARSDK_LOGE("%s:%d: %s err=%d(%s)", __func__, __LINE__, \
			_fct, _err, strerror(_err))

/** Log error with fd and errno */
#define ARSDK_LOG_FD_ERRNO(_fct, _fd, _err) \
	ARSDK_LOGE("%s:%d: %s(fd=%d) err=%d(%s)", __func__, __LINE__, \
			_fct, _fd, _err, strerror(_err))

/** Log error if condition failed and return from function */
#define ARSDK_RETURN_IF_FAILED(_cond, _err) \
	do { \
		if (!(_cond)) { \
			ARSDK_LOGE("%s:%d: err=%d(%s)", __func__, __LINE__, \
					(_err), strerror(-(_err))); \
			return; \
		} \
	} while (0)

/** Log error if condition failed and return error from function */
#define ARSDK_RETURN_ERR_IF_FAILED(_cond, _err) \
	do { \
		if (!(_cond)) { \
			ARSDK_LOGE("%s:%d: err=%d(%s)", __func__, __LINE__, \
					(_err), strerror(-(_err))); \
			/* codecheck_ignore[RETURN_PARENTHESES] */ \
			return (_err); \
		} \
	} while (0)

/** Log error if condition failed and return value from function */
#define ARSDK_RETURN_VAL_IF_FAILED(_cond, _err, _val) \
	do { \
		if (!(_cond)) { \
			ARSDK_LOGE("%s:%d: err=%d(%s)", __func__, __LINE__, \
					(_err), strerror(-(_err))); \
			/* codecheck_ignore[RETURN_PARENTHESES] */ \
			return (_val); \
		} \
	} while (0)

#endif /* !_ARSDK_LOG_H_ */
