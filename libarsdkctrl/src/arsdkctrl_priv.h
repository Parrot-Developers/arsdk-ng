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

#ifndef _ARSDKCTRL_PRIV_H_
#define _ARSDKCTRL_PRIV_H_

/* For gmtime_r access */
#ifdef __MINGW32__
#  define _POSIX_C_SOURCE 2
#endif

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif /* !_GNU_SOURCE */

#include "arsdk_priv.h"

#include <libpomp.h>
#include <futils/timetools.h>

/* Public headers */
#include "arsdkctrl/arsdkctrl.h"
#include "arsdkctrl/internal/arsdkctrl_internal.h"

/* Private headers */
#include "arsdk_list.h"
#include "arsdk_transport_ids.h"
#include "cmd_itf/arsdk_cmd_itf_priv.h"
#include "arsdk_md5_priv.h"
#include "arsdk_device_priv.h"

/** device */
struct arsdk_device {
	struct list_node              node;
	struct arsdkctrl_backend      *backend;
	struct arsdk_discovery        *discovery;
	int32_t                       discovery_runid;
	uint16_t                      handle;
	void                          *osdata;
	int                           deleted;

	struct arsdk_device_info      info;
	char                          *name;
	char                          *addr;
	char                          *id;
	char                          *json;

	struct arsdk_device_conn      *conn;
	struct arsdk_device_conn_cbs  cbs;
	struct arsdk_transport        *transport;
	struct arsdk_cmd_itf          *cmd_itf;
	union {
		struct arsdk_cmd_itf1   *v1;
		struct arsdk_cmd_itf2   *v2;
	} cmd_itf_child;
	struct arsdk_ftp_itf          *ftp_itf;
	struct arsdk_media_itf        *media_itf;
	struct arsdk_updater_itf      *updater_itf;
	struct arsdk_blackbox_itf     *blackbox_itf;
	struct arsdk_crashml_itf      *crashml_itf;
	struct arsdk_flight_log_itf   *flight_log_itf;
	struct arsdk_pud_itf          *pud_itf;
	struct arsdk_ephemeris_itf    *ephemeris_itf;
};

/** backend */
struct arsdkctrl_backend {
	struct list_node                        node;
	char                                    *name;
	enum arsdk_backend_type                 type;
	void                                    *child;
	const struct arsdkctrl_backend_ops      *ops;
	void                                    *osdata;
	struct arsdk_ctrl                       *ctrl;
};

/** discovery */
struct arsdk_discovery {
	struct list_node                node;
	char                            *name;
	struct arsdkctrl_backend        *backend;
	struct arsdk_ctrl               *ctrl;
	struct pomp_timer               *timer;
	int32_t                         runid;
	int                             started;
};

/** device tcp proxy */
struct arsdk_device_tcp_proxy {
	/** device to access. */
	struct arsdk_device *device;
	/** address to connect. */
	char *addr;
	/** port to connect. */
	uint16_t port;
#ifdef LIBMUX_LEGACY
	/** mux tcp proxy. */
	struct mux_tcp_proxy *mux_tcp_proxy;
#else
	/** mux tcp proxy. */
	struct mux_ip_proxy *mux_tcp_proxy;
	/** '1' if closed otherwise '0'. */
	int is_closed;
	/** event callbacks. */
	struct arsdk_device_tcp_proxy_cbs cbs;
#endif
};

int arsdk_device_new(struct arsdkctrl_backend *backend,
		struct arsdk_discovery *discovery,
		int16_t discovery_runid,
		uint16_t handle,
		const struct arsdk_device_info *info,
		struct arsdk_device **ret_obj);

void arsdk_device_destroy(struct arsdk_device *self);

int arsdk_device_clear_backend(struct arsdk_device *self);

int arsdk_device_clear_discovery(struct arsdk_device *self);

int16_t arsdk_device_get_discovery_runid(struct arsdk_device *self);

int arsdk_device_set_discovery_runid(struct arsdk_device *self, int16_t runid);

struct arsdk_discovery *arsdk_device_get_discovery(struct arsdk_device *self);

int arsdk_ctrl_create_device(struct arsdk_ctrl *self,
		struct arsdk_discovery *discovery,
		int16_t discovery_runid,
		const struct arsdk_device_info *info,
		struct arsdk_device **ret_obj);

int arsdk_ctrl_destroy_device(struct arsdk_ctrl *self,
		struct arsdk_device *dev);

int arsdk_ctrl_register_backend(struct arsdk_ctrl *ctrl,
		struct arsdkctrl_backend *backend);

int arsdk_ctrl_unregister_backend(struct arsdk_ctrl *ctrl,
		struct arsdkctrl_backend *backend);

int arsdk_ctrl_register_discovery(struct arsdk_ctrl *ctrl,
		struct arsdk_discovery *discovery);

int arsdk_ctrl_unregister_discovery(struct arsdk_ctrl *ctrl,
		struct arsdk_discovery *discovery);

#endif /* !_ARSDKCTRL_PRIV_H_ */
