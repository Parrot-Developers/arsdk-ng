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

#ifndef _ARSDK_AVAHI_LOOP_H_
#define _ARSDK_AVAHI_LOOP_H_

#ifdef BUILD_AVAHI_CLIENT

#include <avahi-common/watch.h>

struct arsdk_avahi_loop;

/** */
struct arsdk_avahi_loop_cbs {
	void *userdata;

	void (*socketcb)(struct arsdk_avahi_loop *self,
			int fd,
			enum arsdk_socket_kind kind,
			void *userdata);
};

ARSDK_API int arsdk_avahi_loop_new(struct pomp_loop *ploop,
		const struct arsdk_avahi_loop_cbs *cbs,
		struct arsdk_avahi_loop **ret_aloop);

ARSDK_API int arsdk_avahi_loop_destroy(struct arsdk_avahi_loop *aloop);

ARSDK_API const AvahiPoll *arsdk_avahi_loop_get_poll(
		struct arsdk_avahi_loop *aloop);

#endif /* BUILD_AVAHI_CLIENT */

#endif /* _ARSDK_AVAHI_LOOP_H_ */
