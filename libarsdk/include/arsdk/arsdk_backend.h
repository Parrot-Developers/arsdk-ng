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

#ifndef _ARSDK_BACKEND_H_
#define _ARSDK_BACKEND_H_

/** */
enum arsdk_socket_kind {
	ARSDK_SOCKET_KIND_DISCOVERY = 0,
	ARSDK_SOCKET_KIND_CONNECTION,
	ARSDK_SOCKET_KIND_COMMAND,
	ARSDK_SOCKET_KIND_FTP,
	ARSDK_SOCKET_KIND_VIDEO,
};

/** */
enum arsdk_device_type {
	ARSDK_DEVICE_TYPE_UNKNOWN = -1,

	ARSDK_DEVICE_TYPE_BEBOP = 0x0901,
	ARSDK_DEVICE_TYPE_BEBOP_2 = 0x090c,
	ARSDK_DEVICE_TYPE_PAROS = 0x0911,
	ARSDK_DEVICE_TYPE_ANAFI4K = 0x0914,
	ARSDK_DEVICE_TYPE_ANAFI_THERMAL = 0x0919,
	ARSDK_DEVICE_TYPE_CHIMERA = 0x0916,
	ARSDK_DEVICE_TYPE_ANAFI_2 = 0x091a,
	ARSDK_DEVICE_TYPE_ANAFI_UA = 0x091b,
	ARSDK_DEVICE_TYPE_ANAFI_USA = 0x091e,

	ARSDK_DEVICE_TYPE_SKYCTRL = 0x0903,
	ARSDK_DEVICE_TYPE_SKYCTRL_NG = 0x0913,
	ARSDK_DEVICE_TYPE_SKYCTRL_2 = 0x090f,
	ARSDK_DEVICE_TYPE_SKYCTRL_2P = 0x0915,
	ARSDK_DEVICE_TYPE_SKYCTRL_3 = 0x0918,
	ARSDK_DEVICE_TYPE_SKYCTRL_UA = 0x091c,
	ARSDK_DEVICE_TYPE_SKYCTRL_4 = 0x091d,

	ARSDK_DEVICE_TYPE_JS = 0x0902,
	ARSDK_DEVICE_TYPE_JS_EVO_LIGHT = 0x0905,
	ARSDK_DEVICE_TYPE_JS_EVO_RACE = 0x0906,

	ARSDK_DEVICE_TYPE_RS = 0x0900,
	ARSDK_DEVICE_TYPE_RS_EVO_LIGHT = 0x0907,
	ARSDK_DEVICE_TYPE_RS_EVO_BRICK = 0x0909,
	ARSDK_DEVICE_TYPE_RS_EVO_HYDROFOIL = 0x090a,
	ARSDK_DEVICE_TYPE_RS3 = 0x090b,

	ARSDK_DEVICE_TYPE_POWERUP = 0x090d,

	ARSDK_DEVICE_TYPE_EVINRUDE = 0x090e,

	ARSDK_DEVICE_TYPE_WINGX = 0x0910,

	/* Reserved identifiers:
	   Tinos : 0x0912
	   Sequoia : 0x0917
	*/
};

/**
 * Device/peer connection cancellation reasons.
 */
enum arsdk_conn_cancel_reason {
	ARSDK_CONN_CANCEL_REASON_LOCAL,     /**< Locally aborted*/
	ARSDK_CONN_CANCEL_REASON_REMOTE,    /**< Remotely aborted*/
	ARSDK_CONN_CANCEL_REASON_REJECTED,  /**< Rejected by remote */
};

/** */
enum arsdk_link_status {
	ARSDK_LINK_STATUS_KO,  /**< Link is broken */
	ARSDK_LINK_STATUS_OK,  /**< Link is OK again */
};

/** */
enum arsdk_backend_type {
	ARSDK_BACKEND_TYPE_UNKNOWN = -1,  /**< Unknown */
	ARSDK_BACKEND_TYPE_NET = 0,       /**< Wifi/IP network */
	ARSDK_BACKEND_TYPE_BLE = 1,       /**< Bluetooth low energy */
	ARSDK_BACKEND_TYPE_MUX = 2,       /**< Mux (USB) */
};

/** Publisher configuration */
struct arsdk_publisher_cfg {
	const char              *name;     /**< Name to publish */
	enum arsdk_device_type  type;      /**< Type to publish */
	const char              *id;       /**< Id to publish */
};

/** Backend listen callbacks */
struct arsdk_backend_listen_cbs {
	void *userdata;

	void (*conn_req)(struct arsdk_peer *peer,
			const struct arsdk_peer_info *info,
			void *userdata);
};

ARSDK_API const char *arsdk_socket_kind_str(enum arsdk_socket_kind val);

ARSDK_API const char *arsdk_device_type_str(enum arsdk_device_type val);

ARSDK_API const char *arsdk_conn_cancel_reason_str(
		enum arsdk_conn_cancel_reason val);

ARSDK_API const char *arsdk_link_status_str(enum arsdk_link_status val);

ARSDK_API const char *arsdk_backend_type_str(enum arsdk_backend_type val);

#endif /* !_ARSDK_BACKEND_H */
