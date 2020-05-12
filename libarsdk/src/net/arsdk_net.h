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

#ifndef _ARSDK_NET_H_
#define _ARSDK_NET_H_

/* Net specific system headers */
#ifdef _WIN32
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0501
#  endif /* !_WIN32_WINNT */
#  define NOGDI
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else /* !_WIN32 */
#  include <fcntl.h>
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <sys/uio.h>
#  include <netinet/in.h>
#  include <netinet/ip.h>
#  include <arpa/inet.h>
#  include <netdb.h>
# if defined(__linux__) && !defined(ANDROID)
#  /* ifaddrs not available on android */
#  include <ifaddrs.h>
# endif
#endif /* !_WIN32 */

#define ARSDK_NET_DEFAULT_C2D_DATA_PORT  2233
#define ARSDK_NET_DEFAULT_D2C_DATA_PORT  9988

#define ARSDK_NET_DEFAULT_C2D_RTP_PORT   5004
#define ARSDK_NET_DEFAULT_C2D_RTCP_PORT  5005
#define ARSDK_NET_DEFAULT_D2C_RTP_PORT   55004
#define ARSDK_NET_DEFAULT_D2C_RTCP_PORT  55005

#define ARSDK_CONN_JSON_KEY_STATUS                 "status"
#define ARSDK_CONN_JSON_KEY_C2DPORT                "c2d_port"
#define ARSDK_CONN_JSON_KEY_D2CPORT                "d2c_port"
#define ARSDK_CONN_JSON_KEY_CONTROLLER_TYPE        "controller_type"
#define ARSDK_CONN_JSON_KEY_CONTROLLER_NAME        "controller_name"
#define ARSDK_CONN_JSON_KEY_DEVICE_ID              "device_id"
#define ARSDK_CONN_JSON_KEY_C2D_UPDATE_PORT        "c2d_update_port"
#define ARSDK_CONN_JSON_KEY_C2D_USER_PORT          "c2d_user_port"
#define ARSDK_CONN_JSON_KEY_SKYCONTROLLER_VERSION  "skycontroller_version"
#define ARSDK_CONN_JSON_KEY_QOS_MODE               "qos_mode"
/**
 * json key used by the controller to indicate
 * the minimum protocol version supported
 */
#define ARSDK_CONN_JSON_KEY_PROTO_V_MIN            "proto_v_min"
/**
 * json key used by the controller to indicate
 * the maximum protocol version supported
 */
#define ARSDK_CONN_JSON_KEY_PROTO_V_MAX            "proto_v_max"
/** json key used by the device to indicate the chosen protocol version. */
#define ARSDK_CONN_JSON_KEY_PROTO_V                "proto_v"

#ifdef _WIN32

#undef EWOULDBLOCK
#undef EADDRINUSE
#define EWOULDBLOCK	WSAEWOULDBLOCK
#define EADDRINUSE	WSAEADDRINUSE

#define O_NONBLOCK	00004000

#define FD_CLOEXEC	1

#define F_GETFD		1		/**< Get file descriptor flags */
#define F_SETFD		2		/**< Set file descriptor flags */
#define F_GETFL		3		/**< Get file status flags */
#define F_SETFL		4		/**< Set file status flags */

/* codecheck_ignore[NEW_TYPEDEFS] */
typedef int     socklen_t;
/* codecheck_ignore[NEW_TYPEDEFS] */
typedef u_long  in_addr_t;

static inline int win32_close(int fd)
{
	return closesocket((SOCKET)fd);
}

static inline ssize_t win32_recvfrom(int fd, void *buf, size_t len,
		int flags, struct sockaddr *addr, socklen_t *addrlen)
{
	return (ssize_t)recvfrom((SOCKET)fd, buf, (int)len,
			flags, addr, addrlen);
}

static inline int win32_fcntl(int fd, int cmd, ...)
{
	return 0;
}

static inline int win32_getsockopt(int sockfd, int level, int optname,
		void *optval, socklen_t *optlen)
{
	return getsockopt((SOCKET)sockfd, level, optname,
			(char *)optval, optlen);
}

static inline int win32_setsockopt(int sockfd, int level, int optname,
		const void *optval, socklen_t optlen)
{
	return setsockopt((SOCKET)sockfd, level, optname,
			(const char *)optval, optlen);
}

#undef close
#undef recvfrom
#undef fcntl
#undef getsockopt
#undef setsockopt
#undef errno

#define close       win32_close
#define recvfrom    win32_recvfrom
#define fcntl       win32_fcntl
#define getsockopt  win32_getsockopt
#define setsockopt  win32_setsockopt
#define errno       ((int)GetLastError())

#endif /* !_WIN32 */

/* Net specific internal headers */
#include "arsdk_transport_net.h"

#include <json-c/json.h>

static inline struct json_object *get_json_object(struct json_object *obj,
		const char *key)
{
	struct json_object *res = NULL;

#if defined(JSON_C_MAJOR_VERSION) && defined(JSON_C_MINOR_VERSION) && \
	((JSON_C_MAJOR_VERSION == 0 && JSON_C_MINOR_VERSION >= 10) || \
	 (JSON_C_MAJOR_VERSION > 0))
	if (!json_object_object_get_ex(obj, key, &res))
		res = NULL;
#else
	/* json_object_object_get is deprecated started version 0.10 */
	res = json_object_object_get(obj, key);
#endif
	return res;
}

#define ARSDK_NET_DISCOVERY_PORT 44445
#define ARSDK_NET_DISCOVERY_KEY_TYPE "model.id"
#define ARSDK_NET_DISCOVERY_KEY_ID "serial"
#define ARSDK_NET_DISCOVERY_KEY_PORT "port"
#define ARSDK_NET_DISCOVERY_KEY_NAME "name"

#endif /* _ARSDK_NET_H_ */
