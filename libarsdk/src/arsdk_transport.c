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
#include "arsdk_default_log.h"

#define ARSDK_PING_DELAY_LOG_THRESHOLD (100*1000) /* 100 ms */

/** */
struct arsdk_transport {
	const char                        *name;
	void                              *child;
	const struct arsdk_transport_ops  *ops;
	void                              *userdata;
	struct arsdk_transport_cbs        cbs;
	struct pomp_loop                  *loop;
	enum arsdk_link_status            link_status;

	struct {
		struct pomp_timer         *timer;
		uint32_t                  period;
		uint16_t                  next_seq;
		int                       running;
		uint64_t                  start;
		uint64_t                  end;
		uint32_t                  delay;
		uint32_t                  failures;
	} ping;
};

/**
 */
static int send_ping(struct arsdk_transport *self)
{
	int res = 0;
	struct timespec now = {0, 0};
	struct arsdk_transport_header header;
	struct arsdk_transport_payload payload;
	const void *cdata = NULL;
	size_t len = 0;

	/* Check if there is a ping in progress, increment failures */
	if (self->ping.running) {
		self->ping.failures++;
		ARSDK_LOGW("%s ping failures: %d", self->name,
				self->ping.failures);
		if (self->ping.failures >= 3 &&
				self->link_status == ARSDK_LINK_STATUS_OK) {
			ARSDK_LOGE("%s Too many ping failures", self->name);
			arsdk_transport_set_link_status(self,
					ARSDK_LINK_STATUS_KO);
		}
	}
	self->ping.running = 0;

	/* Ping start time */
	if (time_get_monotonic(&now) < 0) {
		res = -errno;
		ARSDK_LOG_ERRNO("time_get_monotonic", errno);
		return res;
	}
	time_timespec_to_us(&now, &self->ping.start);

	/* Don't care about byte order, remote is not supposed to interpret it,
	 * just send it back */
	cdata = &self->ping.start;
	len = sizeof(self->ping.start);

	/* Setup header and payload */
	memset(&header, 0, sizeof(header));
	header.type = ARSDK_TRANSPORT_DATA_TYPE_NOACK;
	header.id = ARSDK_TRANSPORT_ID_PING;
	header.seq = self->ping.next_seq++;

	arsdk_transport_payload_init_with_data(&payload, cdata, len);

	/* Send data */
	res = arsdk_transport_send_data(self, &header, &payload, NULL, 0);
	if (res != 0) {
		ARSDK_LOGW("ping arsdk_transport_send_data: err=%d(%s)",
				-res, strerror(-res));
	}
	self->ping.running = 1;
	arsdk_transport_payload_clear(&payload);
	return res;
}

/**
 */
static int send_pong(struct arsdk_transport *self,
		enum arsdk_transport_data_type type,
		uint16_t seq,
		const struct arsdk_transport_payload *payload)
{
	struct arsdk_transport_header header;

	/* Setup header */
	memset(&header, 0, sizeof(header));
	header.type = type;
	header.id = ARSDK_TRANSPORT_ID_PONG;
	header.seq = seq;

	/* Send data */
	return arsdk_transport_send_data(self, &header, payload, NULL, 0);
}

/**
 */
static void recv_pong(struct arsdk_transport *self,
		const struct arsdk_transport_payload *payload)
{
	struct timespec now = {0, 0};

	/* Is there a ping in progress ? */
	if (!self->ping.running)
		return;

	/* Get data from payload */
	if (payload->cdata == NULL) {
		ARSDK_LOGW("%s PONG: missing payload", self->name);
		return;
	}

	/* Make sure it is the anwser to the current ping */
	if (payload->len != sizeof(self->ping.start)) {
		ARSDK_LOGW("%s PONG: bad payload length: %u", self->name,
				(uint32_t)payload->len);
		return;
	}
	if (memcmp(&self->ping.start, payload->cdata, payload->len) != 0) {
		ARSDK_LOGW("%s PONG: payload mismatch", self->name);
		return;
	}

	/* Ping end time */
	if (time_get_monotonic(&now) < 0) {
		ARSDK_LOG_ERRNO("time_get_monotonic", errno);
		return;
	}
	time_timespec_to_us(&now, &self->ping.end);

	self->ping.running = 0;
	self->ping.failures = 0;
	self->ping.delay = (uint32_t)(self->ping.end - self->ping.start);

	/* Log ping delay > 100 ms */
	if (self->ping.delay >= ARSDK_PING_DELAY_LOG_THRESHOLD) {
		ARSDK_LOGI("%s ping delay: %u.%ums",
				self->name,
				self->ping.delay / 1000,
				self->ping.delay % 1000);
	} else {
		ARSDK_LOGD("%s ping delay: %u.%ums",
				self->name,
				self->ping.delay / 1000,
				self->ping.delay % 1000);
	}

	/* Assume link is OK */
	arsdk_transport_set_link_status(self, ARSDK_LINK_STATUS_OK);
}

/**
 */
static void restart_ping(struct arsdk_transport *self)
{
	int res = 0;
	if (self->ping.period > 0) {
		res = pomp_timer_set_periodic(self->ping.timer,
				self->ping.period,
				self->ping.period);
		if (res < 0)
			ARSDK_LOG_ERRNO("pomp_timer_set_periodic", -res);
	}

	/* Reset ping failures */
	self->ping.failures = 0;
}

/**
 */
static void timer_cb(struct pomp_timer *timer, void *userdata)
{
	struct arsdk_transport *self = userdata;
	send_ping(self);
}

/**
 */
static void notify_link_status_idle(void *userdata)
{
	struct arsdk_transport *self = userdata;
	if (self->cbs.link_status != NULL) {
		(*self->cbs.link_status)(self, self->link_status,
				self->cbs.userdata);
	}
}

/**
 */
int arsdk_transport_new(
		void *child,
		const struct arsdk_transport_ops *ops,
		struct pomp_loop *loop,
		uint32_t ping_period,
		const char *name,
		struct arsdk_transport **ret_obj)
{
	int res = 0;
	struct arsdk_transport *self = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(ops != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ops->dispose != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ops->start != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ops->stop != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ops->send_data != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(loop != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(name != NULL, -EINVAL);

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Initialize structure */
	self->child = child;
	self->name = name;
	self->ops = ops;
	self->loop = loop;
	self->link_status = ARSDK_LINK_STATUS_OK;
	self->ping.period = ping_period;

	/* Create ping timer */
	self->ping.timer = pomp_timer_new(self->loop, &timer_cb, self);
	if (self->ping.timer == NULL) {
		res = -ENOMEM;
		goto error;
	}

	*ret_obj = self;
	return 0;

	/* Cleanup in case of error */
error:
	free(self);
	return res;
}

/**
 */
int arsdk_transport_destroy(struct arsdk_transport *self)
{
	int res = 0;
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	if (self->ops->dispose != NULL) {
		res = (*self->ops->dispose)(self);
		if (res < 0)
			return res;
	}

	if (self->ping.timer != NULL)
		pomp_timer_destroy(self->ping.timer);

	free(self);
	return 0;
}

/**
 */
void *arsdk_transport_get_child(struct arsdk_transport *self)
{
	return self == NULL ? NULL : self->child;
}

/**
 */
struct pomp_loop *arsdk_transport_get_loop(struct arsdk_transport *self)
{
	return self == NULL ? NULL : self->loop;
}

/**
 */
int arsdk_transport_start(struct arsdk_transport *self,
		const struct arsdk_transport_cbs *cbs)
{
	int res = 0;
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->recv_data != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->link_status != NULL, -EINVAL);
	if (self->ops->start == NULL)
		return -ENOSYS;

	/* Start ping timer */
	restart_ping(self);

	/* Call specific start */
	self->cbs = *cbs;
	res = (*self->ops->start)(self);
	if (res < 0)
		goto error;

	/* Assume link status is OK, reset ping data */
	self->link_status = ARSDK_LINK_STATUS_OK;
	self->ping.next_seq = 0;
	self->ping.running = 0;
	self->ping.delay = 0;
	self->ping.failures = 0;
	return 0;

	/* Cleanup in case of error */
error:
	pomp_timer_clear(self->ping.timer);
	return res;
}

/**
 */
int arsdk_transport_stop(struct arsdk_transport *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	if (self->ops->stop == NULL)
		return -ENOSYS;
	pomp_loop_idle_remove(self->loop, &notify_link_status_idle, self);
	pomp_timer_clear(self->ping.timer);
	memset(&self->cbs, 0, sizeof(self->cbs));
	return (*self->ops->stop)(self);
}

/**
 */
int arsdk_transport_send_data(struct arsdk_transport *self,
		const struct arsdk_transport_header *header,
		const struct arsdk_transport_payload *payload,
		const void *extra_hdr,
		size_t extra_hdrlen)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	if (self->ops->send_data == NULL)
		return -ENOSYS;
	return (*self->ops->send_data)(self, header, payload,
			extra_hdr, extra_hdrlen);
}

/**
 */
int arsdk_transport_recv_data(struct arsdk_transport *self,
		const struct arsdk_transport_header *header,
		const struct arsdk_transport_payload *payload)
{
	int res = 0;
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(header != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(payload != NULL, -EINVAL);

	if (header->id == ARSDK_TRANSPORT_ID_PING) {
		send_pong(self, header->type, header->seq, payload);
	} else if (header->id == ARSDK_TRANSPORT_ID_PONG) {
		recv_pong(self, payload);
	} else if (self->cbs.recv_data != NULL) {
		(*self->cbs.recv_data)(self, header, payload,
				self->cbs.userdata);
	} else {
		res = -EIO;
	}

	/* Data received, restart ping timer */
	restart_ping(self);
	return res;
}

/**
 */
int arsdk_transport_set_link_status(struct arsdk_transport *self,
		enum arsdk_link_status status)
{
	int res = 0;
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	if (status == self->link_status)
		return 0;

	/* Notify in idle to avoid destroying live object */
	self->link_status = status;
	res = pomp_loop_idle_add(self->loop, &notify_link_status_idle, self);
	if (res < 0)
		ARSDK_LOG_ERRNO("pomp_loop_idle_add", -res);
	return res;
}

/**
 */
enum arsdk_link_status arsdk_transport_get_link_status(
		struct arsdk_transport *self)
{
	return self == NULL ? ARSDK_LINK_STATUS_KO : self->link_status;
}

/**
 */
void arsdk_transport_log_cmd(
		struct arsdk_transport *self,
		const void *header,
		size_t headerlen,
		const struct arsdk_transport_payload *payload,
		enum arsdk_cmd_dir dir)
{
	if (!self->cbs.log_cb)
		return;

	(*self->cbs.log_cb)(self,
			dir,
			header,
			headerlen,
			payload->cdata,
			payload->len,
			self->cbs.userdata);
}

uint32_t arsdk_transport_get_proto_v(struct arsdk_transport *self)
{
	ARSDK_RETURN_VAL_IF_FAILED(self != NULL, -EINVAL, 0);

	if (self->ops->get_proto_v != NULL)
		return (*self->ops->get_proto_v)(self);
	else
		return 1;
}
