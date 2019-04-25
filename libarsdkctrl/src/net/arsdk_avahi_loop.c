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

#include "arsdk_priv.h"
#include "arsdk_avahi_loop.h"
#include "arsdkctrl_net_log.h"

#ifdef BUILD_AVAHI_CLIENT

/** */
struct arsdk_avahi_loop {
	struct pomp_loop            *ploop;
	AvahiPoll                   apoll;
	struct arsdk_avahi_loop_cbs cbs;
};

/** */
struct AvahiWatch {
	struct arsdk_avahi_loop    *aloop;
	int                        fd;
	AvahiWatchEvent            events;
	AvahiWatchEvent            revents;
	AvahiWatchCallback         callback;
	void                       *userdata;
};

/** */
struct AvahiTimeout {
	struct arsdk_avahi_loop    *aloop;
	struct pomp_timer          *timer;
	AvahiTimeoutCallback       callback;
	void                       *userdata;
};

/**
 */
static uint32_t avahi_events_to_pomp(AvahiWatchEvent events)
{
	uint32_t res = 0;
	if (events & AVAHI_WATCH_IN)
		res |= POMP_FD_EVENT_IN;
	if (events & AVAHI_WATCH_OUT)
		res |= POMP_FD_EVENT_OUT;
	if (events & AVAHI_WATCH_ERR)
		res |= POMP_FD_EVENT_ERR;
	if (events & AVAHI_WATCH_HUP)
		res |= POMP_FD_EVENT_HUP;
	return res;
}

/**
 */
static AvahiWatchEvent avahi_events_from_pomp(uint32_t events)
{
	AvahiWatchEvent res = 0;
	if (events & POMP_FD_EVENT_IN)
		res |= AVAHI_WATCH_IN;
	if (events & POMP_FD_EVENT_OUT)
		res |= AVAHI_WATCH_OUT;
	if (events & POMP_FD_EVENT_ERR)
		res |= AVAHI_WATCH_ERR;
	if (events & POMP_FD_EVENT_HUP)
		res |= AVAHI_WATCH_HUP;
	return res;
}

/**
 */
static int timeval_compare(const struct timeval *a, const struct timeval *b)
{
	if (a->tv_sec < b->tv_sec)
		return -1;

	if (a->tv_sec > b->tv_sec)
		return 1;

	if (a->tv_usec < b->tv_usec)
		return -1;

	if (a->tv_usec > b->tv_usec)
		return 1;

	return 0;
}

/**
 */
static int32_t timeval_diff(const struct timeval *a, const struct timeval *b)
{
	if (timeval_compare(a, b) < 0)
		return -timeval_diff(b, a);
	return (int32_t)((a->tv_sec - b->tv_sec) * 1000 +
			(a->tv_usec - b->tv_usec) / 1000);
}

/**
 */
static int32_t timeval_abs_to_ms(const struct timeval *tv)
{
	struct timeval tvnow;
	gettimeofday(&tvnow, NULL);
	return timeval_diff(tv, &tvnow);
}

/**
 */
static void fd_event_cb(int fd, uint32_t revents, void *userdata)
{
	AvahiWatch *w = userdata;
	w->revents = avahi_events_from_pomp(revents);
	(*w->callback)(w, w->fd, w->revents, w->userdata);
	w->revents = 0;
}

/**
 */
static void timer_cb(struct pomp_timer *timer, void *userdata)
{
	AvahiTimeout *t = userdata;
	(*t->callback)(t, t->userdata);
}

/**
 */
static AvahiWatch *watch_new(const AvahiPoll *api,
		int fd,
		AvahiWatchEvent events,
		AvahiWatchCallback callback,
		void *userdata)
{
	int res = 0;
	struct arsdk_avahi_loop *aloop = api->userdata;
	struct AvahiWatch *w = NULL;

	/* Allocate AvahiWatch structure */
	w = calloc(1, sizeof(*w));
	if (w == NULL)
		return NULL;

	/* Initialize structure */
	w->aloop = aloop;
	w->fd = fd;
	w->events = events;
	w->callback = callback;
	w->userdata = userdata;

	/* Register in loop */
	res = pomp_loop_add(aloop->ploop, fd, avahi_events_to_pomp(events),
			&fd_event_cb, w);
	if (res < 0) {
		ARSDK_LOG_ERRNO("pomp_loop_add", -res);
		free(w);
		return NULL;
	}

	/* socket hook callback */
	aloop->cbs.socketcb(aloop, fd, ARSDK_SOCKET_KIND_DISCOVERY,
			aloop->cbs.userdata);

	return w;
}

/**
 */
static void watch_update(AvahiWatch *w, AvahiWatchEvent events)
{
	int res = 0;
	res = pomp_loop_update(w->aloop->ploop, w->fd,
			avahi_events_to_pomp(events));
	if (res < 0)
		ARSDK_LOG_ERRNO("pomp_loop_update", -res);
}

/**
 */
static AvahiWatchEvent watch_get_events(AvahiWatch *w)
{
	return w->revents;
}

/**
 */
static void watch_free(AvahiWatch *w)
{
	pomp_loop_remove(w->aloop->ploop, w->fd);
	free(w);
}

/**
 */
static AvahiTimeout *timeout_new(const AvahiPoll *api,
		const struct timeval *tv,
		AvahiTimeoutCallback callback,
		void *userdata)
{
	int res = 0;
	struct arsdk_avahi_loop *aloop = api->userdata;
	AvahiTimeout *t = NULL;
	int32_t delay = 0;

	/* Allocate AvahiTimeout structure */
	t = calloc(1, sizeof(*t));
	if (t == NULL)
		return NULL;

	/* Initialize structure */
	t->aloop = aloop;
	t->timer = pomp_timer_new(aloop->ploop, &timer_cb, t);
	t->callback = callback;
	t->userdata = userdata;
	if (t->timer == NULL) {
		free(t);
		return NULL;
	}

	/* Setup initial delay */
	if (tv != NULL) {
		/* If delay is <= 0 it means the timer should expire now
		 * pomp timer does not support 0ms delay so we fake it by
		 * specifying 1ms. Another solution might be to call timer
		 * callback now, however I don't known how avahi will react
		 */
		if (tv->tv_sec != 0 || tv->tv_usec != 0)
			delay = timeval_abs_to_ms(tv);
		if (delay > 0)
			res = pomp_timer_set(t->timer, (uint32_t)delay + 1);
		else
			res = pomp_timer_set(t->timer, 1);
		if (res < 0)
			ARSDK_LOG_ERRNO("pomp_timer_set", -res);
	}

	return t;
}

/**
 */
static void timeout_update(AvahiTimeout *t, const struct timeval *tv)
{
	int res = 0;
	int32_t delay = 0;

	if (tv != NULL) {
		/* See comment in timeout_new about 1ms trick */
		if (tv->tv_sec != 0 || tv->tv_usec != 0)
			delay = timeval_abs_to_ms(tv);
		if (delay > 0)
			res = pomp_timer_set(t->timer, (uint32_t)delay + 1);
		else
			res = pomp_timer_set(t->timer, 1);
		if (res < 0)
			ARSDK_LOG_ERRNO("pomp_timer_set", -res);
	} else {
		pomp_timer_clear(t->timer);
	}
}

/**
 */
static void timeout_free(AvahiTimeout *t)
{
	pomp_timer_destroy(t->timer);
	free(t);
}

/**
 */
int arsdk_avahi_loop_new(struct pomp_loop *ploop,
		const struct arsdk_avahi_loop_cbs *cbs,
		struct arsdk_avahi_loop **ret_aloop)
{
	struct arsdk_avahi_loop *aloop = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_aloop != NULL, -EINVAL);
	*ret_aloop = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(ploop != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->socketcb != NULL, -EINVAL);

	/* Allocate structure */
	aloop = calloc(1, sizeof(*aloop));
	if (aloop == NULL)
		return -ENOMEM;

	/* Initialize structure */
	aloop->ploop = ploop;
	aloop->cbs = *cbs;

	/* Setup avahi poll */
	aloop->apoll.userdata = aloop;
	aloop->apoll.watch_new = &watch_new;
	aloop->apoll.watch_update = &watch_update;
	aloop->apoll.watch_get_events = &watch_get_events;
	aloop->apoll.watch_free = &watch_free;
	aloop->apoll.timeout_new = &timeout_new;
	aloop->apoll.timeout_update = &timeout_update;
	aloop->apoll.timeout_free = &timeout_free;

	*ret_aloop = aloop;
	return 0;
}

/**
 */
int arsdk_avahi_loop_destroy(struct arsdk_avahi_loop *aloop)
{
	ARSDK_RETURN_ERR_IF_FAILED(aloop != NULL, -EINVAL);
	free(aloop);
	return 0;
}

/**
 */
const AvahiPoll *arsdk_avahi_loop_get_poll(struct arsdk_avahi_loop *aloop)
{
	return aloop == NULL ? NULL : &aloop->apoll;
}

#endif /* BUILD_AVAHI_CLIENT */
