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

#ifndef _ARSDK_DISCOVERY_INTERNAL_H_
#define _ARSDK_DISCOVERY_INTERNAL_H_

#include <arsdkctrl/arsdkctrl.h>

struct arsdk_discovery_device_info {
	const char              *name;
	enum arsdk_device_type  type;
	const char              *addr;
	uint16_t                port;
	const char              *id;
	uint32_t                proto_v; /**< protocol version */
	enum arsdk_device_api   api; /**< api capabilites */
};

struct arsdk_discovery;

ARSDK_API int arsdk_discovery_new(const char *name,
		struct arsdkctrl_backend *backend,
		struct arsdk_ctrl *ctrl,
		struct arsdk_discovery **ret_obj);

ARSDK_API int arsdk_discovery_destroy(struct arsdk_discovery *self);

ARSDK_API int arsdk_discovery_start(struct arsdk_discovery *self);

ARSDK_API int arsdk_discovery_stop(struct arsdk_discovery *self);

ARSDK_API int arsdk_discovery_add_device(struct arsdk_discovery *self,
		const struct arsdk_discovery_device_info *info);

ARSDK_API int arsdk_discovery_remove_device(struct arsdk_discovery *self,
		const struct arsdk_discovery_device_info *info);

#endif /* _ARSDK_DISCOVERY_INTERNAL_H_ */
