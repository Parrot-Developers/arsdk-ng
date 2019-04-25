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

#ifndef _ARSDK_H_
#define _ARSDK_H_

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include <libpomp.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** To be used for all public API */
#ifdef ARSDK_API_EXPORTS
#  ifdef _WIN32
#    define ARSDK_API	__declspec(dllexport)
#  else /* !_WIN32 */
#    define ARSDK_API	__attribute__((visibility("default")))
#  endif /* !_WIN32 */
#else /* !ARSDK_API_EXPORTS */
#  define ARSDK_API
#endif /* !ARSDK_API_EXPORTS */

/* Internal forward declarations */
struct arsdk_mngr;
struct arsdk_backend;
struct arsdk_cmd;
struct arsdk_cmd_desc;
struct arsdk_cmd_itf;
struct arsdk_peer;
struct arsdk_peer_info;

#include "arsdk_desc.h"
#include "arsdk_cmd_itf.h"

#include "arsdk_mngr.h"
#include "arsdk_backend.h"
#include "arsdk_backend_net.h"
#include "arsdk_backend_mux.h"
#include "arsdk_publisher_avahi.h"
#include "arsdk_publisher_net.h"
#include "arsdk_publisher_mux.h"
#include "arsdk_peer.h"


/* Generated files */
#include "arsdk_ids.h"
#include "arsdk_enums.h"
#include "arsdk_multisettings.h"
#include "arsdk_cmd_desc.h"
#include "arsdk_cmd_dec.h"
#include "arsdk_cmd_enc.h"
#include "arsdk_cmd_send.h"

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !_ARSDK_H_ */
