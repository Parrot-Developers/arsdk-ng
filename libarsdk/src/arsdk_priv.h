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

#ifndef _ARSDK_PRIV_H_
#define _ARSDK_PRIV_H_

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif /* !_GNU_SOURCE */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>
#include <stddef.h>

#ifndef _MSC_VER
#  include <unistd.h>
#endif /* !_MSC_VER */

#include <libpomp.h>
#include <futils/timetools.h>

/* Public headers */
#include "arsdk/arsdk.h"
#include "arsdk/internal/arsdk_internal.h"

/* Private headers */
#include "arsdk_list.h"
#include "arsdk_transport_ids.h"
#include "arsdk_cmd_itf_priv.h"

/** Endianess detection */
#if !defined(ARSDK_LITTLE_ENDIAN) && !defined(ARSDK_BIG_ENDIAN)
#  if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#    define ARSDK_LITTLE_ENDIAN
#  elif defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#    define ARSDK_BIG_ENDIAN
#  elif defined(_WIN32)
#    define ARSDK_LITTLE_ENDIAN
#  else
#    ifdef __APPLE__
#      include <machine/endian.h>
#    else
#      include <endian.h>
#    endif
#    if defined(BYTE_ORDER) && (BYTE_ORDER == LITTLE_ENDIAN)
#      define ARSDK_LITTLE_ENDIAN
#    elif defined(BYTE_ORDER) && (BYTE_ORDER == BIG_ENDIAN)
#      define ARSDK_BIG_ENDIAN
#    else
#      error Unable to determine endianess of machine
#    endif
#  endif
#endif

#ifdef ARSDK_LITTLE_ENDIAN

/** Convert 16-bit integer from host ordering to little endian */
#define ARSDK_HTOLE16(_x)	((uint16_t)(_x))

/** Convert 32-bit integer from host ordering to little endian */
#define ARSDK_HTOLE32(_x)	((uint32_t)(_x))

/** Convert 64-bit integer from host ordering to little endian */
#define ARSDK_HTOLE64(_x)	((uint64_t)(_x))

/** Convert 16-bit integer from little endian to host ordering */
#define ARSDK_LE16TOH(_x)	((uint16_t)(_x))

/** Convert 32-bit integer from little endian to host ordering */
#define ARSDK_LE32TOH(_x)	((uint32_t)(_x))

/** Convert 64-bit integer from little endian to host ordering */
#define ARSDK_LE64TOH(_x)	((uint64_t)(_x))

#else

#error Big endian machines not yet supported

#endif

#ifndef PRIi64
#  define PRIi64 "lli"
#endif /* !PRIi64 */
#ifndef PRIu64
#  define PRIu64 "llu"
#endif /* !PRIu64 */

/* Signed size_t type */
#ifdef _MSC_VER
#  ifdef _WIN64
/* codecheck_ignore[NEW_TYPEDEFS] */
typedef signed __int64  ssize_t;
#  else /* !_WIN64 */
/* codecheck_ignore[NEW_TYPEDEFS] */
typedef _W64 signed int ssize_t;
#  endif /* !_WIN64 */
#endif /* _MSC_VER */

/** */
struct arsdk_cmd_itf {
	void                               *osdata;
	struct arsdk_cmd_itf_cbs           cbs;
	struct arsdk_cmd_itf_internal_cbs  internal_cbs;
	struct arsdk_transport             *transport;
	struct pomp_loop                   *loop;
	struct pomp_timer                  *timer;
	struct queue                       **tx_queues;
	uint32_t                           tx_count;
	uint8_t                            ackoff;
	uint8_t                            next_ack_seq;
	uint8_t                            recv_seq[UINT8_MAX+1];
	struct {
		struct pomp_timer          *timer;
		uint32_t                   retry_count;
		uint32_t                   ack_count;
		uint32_t                   rx_miss_count;
		uint32_t                   rx_useless_count;
		uint32_t                   rx_useful_count;
	} lnqlt;
};

/** peer */
struct arsdk_peer {
	struct list_node            node;
	struct arsdk_backend        *backend;
	uint16_t                    handle;
	void                        *osdata;
	int                         deleting;

	struct arsdk_peer_info      info;
	char                        *ctrl_name;
	char                        *ctrl_type;
	char                        *ctrl_addr;
	char                        *device_id;
	char                        *json;

	struct arsdk_peer_conn      *conn;
	struct arsdk_peer_conn_cbs  cbs;
	struct arsdk_transport      *transport;
	struct arsdk_cmd_itf        *cmd_itf;
};

/** backend */
struct arsdk_backend {
	struct list_node                node;
	char                            *name;
	enum arsdk_backend_type         type;
	void                            *child;
	const struct arsdk_backend_ops  *ops;
	void                            *osdata;
	struct arsdk_mngr               *mngr;
};

/**
 */
static inline char *xstrdup(const char *s)
{
	return s == NULL ? NULL : strdup(s);
}

#ifdef _WIN32

static inline char *strndup(const char *s, size_t n)
{
	size_t len = strnlen(s, n);
	char *p = malloc(len + 1);
	if (p != NULL) {
		memcpy(p, s, len);
		p[len] = '\0';
	}
	return p;
}

static inline char *strptime(const char *s, const char *format, struct tm *tm)
{
	/* TODO: implements strptime for windows */
	return NULL;
}

static inline void srandom(unsigned int seed)
{
	srand(seed);
}

static inline long int random(void)
{
	return rand();
}

#endif /* !_WIN32 */

int arsdk_peer_new(struct arsdk_backend *backend,
		const struct arsdk_peer_info *info,
		uint16_t handle,
		struct arsdk_peer_conn *conn,
		struct arsdk_peer **ret_obj);

void arsdk_peer_destroy(struct arsdk_peer *self);

int arsdk_mngr_create_peer(struct arsdk_mngr *self,
		struct arsdk_backend *backend,
		const struct arsdk_peer_info *info,
		struct arsdk_peer_conn *conn,
		struct arsdk_peer **ret_obj);

int arsdk_mngr_destroy_peer(struct arsdk_mngr *self,
		struct arsdk_peer *peer);

int arsdk_mngr_register_backend(struct arsdk_mngr *mngr,
		struct arsdk_backend *backend);

int arsdk_mngr_unregister_backend(struct arsdk_mngr *mngr,
		struct arsdk_backend *backend);

#endif /* !_ARSDK_PRIV_H_ */
