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
#include "cmd_itf/arsdk_cmd_itf_priv.h"
#include "cmd_itf/arsdk_cmd_itf1.h"
#include "arsdk_default_log.h"

/** link quality analysis frequency */
#define LINK_QUALITY_TIME_MS 5000

/** */
struct entry {
	struct arsdk_cmd                        cmd;
	arsdk_cmd_itf_cmd_send_status_cb_t      send_status;
	void                                    *userdata;
	uint8_t                                 seq;
	int                                     waiting_ack;
	int                                     retry_count;
	int32_t                                 max_retry_count;
	struct timespec                         sent_ts;
};

/** */
struct queue {
	struct arsdk_cmd_queue_info  info;
	struct entry                 *entries;
	uint32_t                     count;
	uint32_t                     depth;
	uint32_t                     head;
	uint32_t                     tail;
	struct timespec              last_sent_ts;
	uint8_t                      seq;
};

/** */
struct arsdk_cmd_itf1 {
	struct arsdk_cmd_itf1_cbs          cbs;
	struct arsdk_cmd_itf_cbs           itf_cbs;
	struct arsdk_cmd_itf               *itf;

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

/**
 */
static void cmd_log(struct arsdk_cmd_itf1 *self,
		const struct arsdk_cmd *cmd, enum arsdk_cmd_dir dir)
{
	if (!self->itf_cbs.cmd_log)
		return;

	(*self->itf_cbs.cmd_log)(self->itf, dir, cmd, self->itf_cbs.userdata);
}

/**
 */
static void entry_init(struct entry *entry,
		const struct arsdk_cmd *cmd,
		arsdk_cmd_itf_cmd_send_status_cb_t send_status,
		void *userdata,
		int32_t default_retry_count)
{
	const struct arsdk_cmd_desc *cmd_desc = NULL;
	memset(entry, 0, sizeof(*entry));
	arsdk_cmd_copy(&entry->cmd, cmd);
	entry->send_status = send_status;
	entry->userdata = userdata;

	cmd_desc = arsdk_cmd_find_desc(cmd);
	if (cmd_desc &&
	    cmd_desc->timeout_policy == ARSDK_CMD_TIMEOUT_POLICY_RETRY)
		entry->max_retry_count = INT32_MAX;
	else
		entry->max_retry_count = default_retry_count;
}

/**
 */
static void entry_clear(struct entry *entry)
{
	/* Release buffer of command, clear entry for safety */
	arsdk_cmd_clear(&entry->cmd);
	memset(entry, 0, sizeof(*entry));
}

/**
 */
static void entry_notify(struct entry *entry, struct arsdk_cmd_itf1 *self,
		enum arsdk_cmd_itf_cmd_send_status status, int done)
{
	/* Notify callback */
	if (entry->send_status != NULL) {
		(*entry->send_status)(self->itf, &entry->cmd,
				ARSDK_CMD_BUFFER_TYPE_INVALID, status, 0, done,
				entry->userdata);
	}
}

/**
 */
static int queue_new(const struct arsdk_cmd_queue_info *info,
		struct queue **ret_queue)
{
	struct queue *queue = NULL;
	*ret_queue = NULL;

	/* Allocate structure */
	queue = calloc(1, sizeof(*queue));
	if (queue == NULL)
		return -ENOMEM;

	/* Initialize structure */
	memcpy(&queue->info, info, sizeof(*info));

	/* Sequence number will wrap to 0 before sending first packet */
	queue->seq = UINT8_MAX;

	*ret_queue = queue;
	return 0;
}

/**
 */
static void queue_stop(struct queue *queue, struct arsdk_cmd_itf1 *self)
{
	uint32_t i = 0, pos = 0;
	struct entry *entry = NULL;

	/* Cancel all entries of queue */
	pos = queue->head;
	for (i = 0; i < queue->count; i++) {
		entry = &queue->entries[pos];
		entry_notify(entry, self,
				ARSDK_CMD_ITF_CMD_SEND_STATUS_CANCELED, 1);
		entry_clear(entry);

		/* Continue in circular buffer */
		pos++;
		if (pos >= queue->depth)
			pos = 0;
	}
	queue->head = queue->tail = queue->count = 0;
}

/**
 */
static int queue_destroy(struct queue *queue, struct arsdk_cmd_itf1 *self)
{
	if (queue->count != 0)
		return -EBUSY;
	free(queue->entries);
	free(queue);
	return 0;
}

/**
 */
static int queue_replace(struct queue *queue,
		struct arsdk_cmd_itf1 *itf,
		const struct arsdk_cmd *cmd,
		arsdk_cmd_itf_cmd_send_status_cb_t send_status,
		void *userdata)
{
	uint32_t i = 0, pos = 0;
	struct entry *entry = NULL;

	pos = queue->head;
	for (i = 0; i < queue->count; i++) {
		entry = &queue->entries[pos];
		if (!entry->waiting_ack && entry->cmd.id == cmd->id)
			goto replace;

		/* Continue in circular buffer */
		pos++;
		if (pos >= queue->depth)
			pos = 0;
	}

	/* Not found */
	return -ENOENT;

replace:
	/* First cancel current entry, they replace it */
	entry_notify(entry, itf, ARSDK_CMD_ITF_CMD_SEND_STATUS_CANCELED, 1);
	entry_clear(entry);
	entry_init(entry, cmd, send_status, userdata,
		   queue->info.default_max_retry_count);
	return 0;
}

/**
 */
static int queue_add(struct queue *queue,
		struct arsdk_cmd_itf1 *itf,
		const struct arsdk_cmd *cmd,
		arsdk_cmd_itf_cmd_send_status_cb_t send_status,
		void *userdata)
{
	uint32_t newdepth = 0;
	struct entry *newentries = NULL;
	struct entry *entry = NULL;
	uint32_t cnt1 = 0, cnt2 = 0;

	/* If queue is configured for overwrite, try to replace an existing
	 * entry */
	if (queue->info.overwrite && queue_replace(queue, itf, cmd,
			send_status, userdata) == 0) {
		return 0;
	}

	/* Grow entries if needed (before it becomes full)
	 * As we are using a circular queue, it is easier to alloc a new array
	 * and copy existing entries at start of it */
	if (queue->count + 1 >= queue->depth) {
		newdepth = queue->depth + 16;
		newentries = calloc(newdepth, sizeof(struct entry));
		if (newentries == NULL)
			return -ENOMEM;

		/* Copy current entries at start of new array*/
		if (queue->head < queue->tail) {
			memcpy(&newentries[0],
					&queue->entries[queue->head],
					queue->count * sizeof(struct entry));
		} else if (queue->head > queue->tail) {
			cnt1 = queue->depth - queue->head;
			cnt2 = queue->tail;
			memcpy(&newentries[0],
					&queue->entries[queue->head],
					cnt1 * sizeof(struct entry));
			memcpy(&newentries[cnt1],
					&queue->entries[0],
					cnt2 * sizeof(struct entry));
		}

		/* Update queue */
		free(queue->entries);
		queue->head = 0;
		queue->tail = queue->count;
		queue->entries = newentries;
		queue->depth = newdepth;
	}

	/* Add in queue */
	entry = &queue->entries[queue->tail];
	entry_init(entry, cmd, send_status, userdata,
		   queue->info.default_max_retry_count);
	queue->tail++;
	if (queue->tail >= queue->depth)
		queue->tail = 0;
	queue->count++;

	return 0;
}

/**
 */
static void queue_pop(struct queue *queue)
{
	struct entry *entry = &queue->entries[queue->head];
	entry_clear(entry);
	queue->head++;
	if (queue->head >= queue->depth)
		queue->head = 0;
	queue->count--;
}

/**
 */
static struct queue *find_tx_queue(struct arsdk_cmd_itf1 *self,
		const struct arsdk_cmd *cmd)
{
	uint32_t i = 0;
	const struct arsdk_cmd_desc *cmd_desc = NULL;
	struct queue *queue = NULL;
	enum arsdk_transport_data_type type = ARSDK_TRANSPORT_DATA_TYPE_UNKNOWN;
	enum arsdk_cmd_buffer_type buffer_type = ARSDK_CMD_BUFFER_TYPE_INVALID;

	/* Take buffer type from cmd if valid */
	if (cmd->buffer_type != ARSDK_CMD_BUFFER_TYPE_INVALID) {
		buffer_type = cmd->buffer_type;
	} else {
		/* Else, get it from command description */
		cmd_desc = arsdk_cmd_find_desc(cmd);
		if (cmd_desc == NULL) {
			ARSDK_LOGW("Unable to find cmd description: %u,%u,%u",
					cmd->prj_id, cmd->cls_id, cmd->cmd_id);
			return NULL;
		}
		buffer_type = cmd_desc->buffer_type;
	}

	/* Search suitable queue */
	for (i = 0; i < self->tx_count; i++) {
		queue = self->tx_queues[i];
		type = queue->info.type;
		switch (buffer_type) {
		case ARSDK_CMD_BUFFER_TYPE_NON_ACK:
			if (type == ARSDK_TRANSPORT_DATA_TYPE_NOACK)
				return queue;
			break;

		case ARSDK_CMD_BUFFER_TYPE_ACK:
			if (type == ARSDK_TRANSPORT_DATA_TYPE_WITHACK)
				return queue;
			break;

		case ARSDK_CMD_BUFFER_TYPE_HIGH_PRIO:
			if (type == ARSDK_TRANSPORT_DATA_TYPE_WITHACK &&
			    queue->info.default_max_retry_count == INT32_MAX)
				return queue;
			break;

		default:
			ARSDK_LOGW("Unknown buffer type: %d", buffer_type);
			break;
		}
	}

	/* No suitable queue found */
	ARSDK_LOGW("Unable to find suitable queue for cmd: %u,%u,%u",
			cmd->prj_id, cmd->cls_id, cmd->cmd_id);
	return NULL;
}

/**
 */
static void check_tx_queue(struct arsdk_cmd_itf1 *self,
		const struct timespec *tsnow,
		struct queue *queue,
		int *next_timeout_ms)
{
	int res = 0;
	uint64_t diff_us = 0;
	int diff_ms = 0;
	int remaining_ms = 0;
	struct entry *entry = NULL;
	int max_retry_count = 0;
	struct arsdk_transport_header header;
	struct arsdk_transport_payload payload;
	size_t len = 0;

again:

	/* Nothing to do if queue is empty */
	if (queue->count == 0)
		return;

	/* Get next entry */
	entry = &queue->entries[queue->head];
	max_retry_count = entry->max_retry_count;

	/* If waiting for an ack, compute next time of check */
	if (entry->waiting_ack) {
		if (time_timespec_diff_in_range(
				&entry->sent_ts,
				tsnow,
				(uint64_t)queue->info.ack_timeout_ms * 1000,
				&diff_us)) {
			/* Still need to wait for ack */
			diff_ms = (int)(diff_us / 1000);
			remaining_ms = queue->info.ack_timeout_ms - diff_ms;

			/* If the remaining time is less than a milisecond,
			   retry now. We should NEVER set next_timeout_ms
			   to zero here, as it would deactivate the pomp_timer
			*/
			if (remaining_ms > 0) {
				if (*next_timeout_ms < 0 ||
				    remaining_ms < *next_timeout_ms)
					*next_timeout_ms = remaining_ms;
				return;
			}
		}

		/* No ack received, do we need a retry ? */
		if (max_retry_count > 0 &&
				entry->retry_count >= max_retry_count) {
			/* Max retry count reached, notify timeout and
			 * continue with next entry in queue */
			entry_notify(entry, self,
				ARSDK_CMD_ITF_CMD_SEND_STATUS_TIMEOUT,
				1);
			queue_pop(queue);
			goto again;
		}

		/* Retry sending command */
		entry->waiting_ack = 0;
		entry->retry_count++;
		memset(&entry->sent_ts, 0, sizeof(entry->sent_ts));
		self->lnqlt.retry_count++;
	}

	/* If delay between tx is not passed, compute next time of check */
	if (queue->info.max_tx_rate_ms > 0 && time_timespec_diff_in_range(
			&queue->last_sent_ts,
			tsnow,
			(uint64_t)queue->info.max_tx_rate_ms * 1000,
			&diff_us)) {
		/* Still need to wait before sending */
		diff_ms = (int)(diff_us / 1000);
		if (*next_timeout_ms < 0 || diff_ms < *next_timeout_ms)
			*next_timeout_ms = diff_ms;
		return;
	}

	/* Determine command buffer length */
	pomp_buffer_get_cdata(entry->cmd.buf, NULL, &len, NULL);

	/* do not increment seq num for retries */
	if (entry->retry_count == 0)
		queue->seq++;

	/* Construct header and payload */
	memset(&header, 0, sizeof(header));
	header.type = queue->info.type;
	header.id = queue->info.id;
	header.seq = queue->seq;

	arsdk_transport_payload_init_with_buf(&payload, entry->cmd.buf);

	/* Send it */
	res = arsdk_transport_send_data(self->transport, &header, &payload,
			NULL, 0);
	arsdk_transport_payload_clear(&payload);
	if (res < 0)
		return;

	entry_notify(entry, self, ARSDK_CMD_ITF_CMD_SEND_STATUS_PACKED,
			queue->info.type != ARSDK_TRANSPORT_DATA_TYPE_WITHACK);
	queue->last_sent_ts = *tsnow;
	if (queue->info.type == ARSDK_TRANSPORT_DATA_TYPE_WITHACK) {
		entry->seq = header.seq;
		entry->waiting_ack = 1;
		entry->sent_ts = *tsnow;
		/* update ack timeout */
		if (queue->info.ack_timeout_ms > 0) {
			diff_ms = queue->info.ack_timeout_ms;
			if (*next_timeout_ms < 0 || diff_ms < *next_timeout_ms)
				*next_timeout_ms = diff_ms;
		}
	} else {
		queue_pop(queue);
		goto again;
	}
}

/**
 */
static void check_tx_queues(struct arsdk_cmd_itf1 *self)
{
	int res = 0;
	uint32_t i = 0;
	struct queue *queue = NULL;
	struct timespec tsnow;
	int next_timeout_ms = -1;

	if (time_get_monotonic(&tsnow) < 0) {
		ARSDK_LOG_ERRNO("time_get_monotonic", errno);
		return;
	}

	/* Check all queues */
	for (i = 0; i < self->tx_count; i++) {
		queue = self->tx_queues[i];
		check_tx_queue(self, &tsnow, queue, &next_timeout_ms);
	}

	/* Update next timeout */
	if (next_timeout_ms > 0) {
		res = pomp_timer_set(self->timer, (uint32_t) next_timeout_ms);
		if (res < 0)
			ARSDK_LOG_ERRNO("pomp_timer_set", -res);
	} else {
		res = pomp_timer_clear(self->timer);
		if (res < 0)
			ARSDK_LOG_ERRNO("pomp_timer_clear", -res);
	}
}

/**
 */
static void recv_ack(struct arsdk_cmd_itf1 *self,
		const struct arsdk_transport_header *header,
		const struct arsdk_transport_payload *payload)
{
	uint8_t seq = 0, id = 0;
	uint32_t i = 0;
	struct queue *queue = NULL;
	struct entry *entry = NULL;
	char cmdbuf[512] = "";

	if (payload->cdata == NULL) {
		ARSDK_LOGW("ACK: missing seq");
		return;
	}
	if (payload->len < 1) {
		ARSDK_LOGW("ACK: missing seq");
		return;
	}
	memcpy(&seq, payload->cdata, sizeof(uint8_t));
	id = header->id - self->ackoff;

	for (i = 0; i < self->tx_count; i++) {
		queue = self->tx_queues[i];
		if (queue->info.id != id)
			continue;

		if (queue->count == 0) {
			ARSDK_LOGD("ACK: no entry pending for id %u", id);
			return;
		}

		entry = &queue->entries[queue->head];
		if (!entry->waiting_ack) {
			ARSDK_LOGD("ACK: no entry pending for id %u", id);
			return;
		}

		if (entry->seq != seq) {
			arsdk_cmd_fmt(&entry->cmd, cmdbuf, sizeof(cmdbuf));
			ARSDK_LOGD("ACK: Bad seq for id %u (%d/%d): %s", id,
				   seq, entry->seq, cmdbuf);
			return;
		}

		self->lnqlt.ack_count++;
		entry_notify(entry, self,
				ARSDK_CMD_ITF_CMD_SEND_STATUS_ACK_RECEIVED, 1);
		queue_pop(queue);
		return;
	}

	ARSDK_LOGW("ACK: unknown id %u", id);
}

/**
 */
static int send_ack(struct arsdk_cmd_itf1 *self, uint8_t id, uint8_t seq)
{
	int res = 0;
	struct arsdk_transport_header header;
	struct arsdk_transport_payload payload;

	/* Construct data with given frame's seq as data */
	memset(&header, 0, sizeof(header));
	header.type = ARSDK_TRANSPORT_DATA_TYPE_ACK;
	header.id = id + self->ackoff;
	header.seq = self->next_ack_seq++;
	arsdk_transport_payload_init_with_data(&payload, &seq, 1);

	/* Send it */
	res = arsdk_transport_send_data(self->transport, &header, &payload,
			NULL, 0);
	arsdk_transport_payload_clear(&payload);
	return res;
}

/**
 */
static void link_quality_timer_cb(struct pomp_timer *timer, void *userdata)
{
	struct arsdk_cmd_itf1 *self = userdata;
	int32_t tx_quality = -1;
	uint32_t tx_sum = 0;

	int32_t rx_quality = -1;
	uint32_t recv_count = 0;
	uint32_t rx_sum = 0;
	int32_t rx_useful = -1;

	ARSDK_RETURN_IF_FAILED(self != NULL, -EINVAL);

	tx_sum = self->lnqlt.retry_count + self->lnqlt.ack_count;
	if (tx_sum > 0)
		tx_quality = (self->lnqlt.ack_count * 100) / tx_sum;

	recv_count = self->lnqlt.rx_useful_count + self->lnqlt.rx_useless_count;
	if (recv_count > 0) {
		rx_sum = self->lnqlt.rx_miss_count + recv_count;

		rx_quality = (recv_count * 100) / rx_sum;

		rx_useful = (self->lnqlt.rx_useful_count * 100) / recv_count;
	}

	/* Link quality callback */
	if (self->itf_cbs.link_quality)
		(*self->itf_cbs.link_quality)(self->itf, tx_quality, rx_quality,
				rx_useful, self->itf_cbs.userdata);

	/* Link quality reset */
	self->lnqlt.ack_count = 0;
	self->lnqlt.retry_count = 0;
	self->lnqlt.rx_useful_count = 0;
	self->lnqlt.rx_useless_count = 0;
	self->lnqlt.rx_miss_count = 0;
}

/**
 */
static void timer_cb(struct pomp_timer *timer, void *userdata)
{
	struct arsdk_cmd_itf1 *self = userdata;
	check_tx_queues(self);
}

/**
 */
int arsdk_cmd_itf1_stop(struct arsdk_cmd_itf1 *self)
{
	uint32_t i = 0;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	/* Stop tx queues */
	if (self->tx_queues != NULL) {
		for (i = 0; i < self->tx_count; i++) {
			if (self->tx_queues[i] != NULL)
				queue_stop(self->tx_queues[i], self);
		}
	}

	self->transport = NULL;
	return 0;
}

/**
 */
int arsdk_cmd_itf1_send(struct arsdk_cmd_itf1 *self,
		const struct arsdk_cmd *cmd,
		arsdk_cmd_itf_cmd_send_status_cb_t send_status,
		void *userdata)
{
	int res = 0;
	struct queue *queue = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(cmd != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	if (self->transport == NULL)
		return -EPIPE;

	cmd_log(self, cmd, ARSDK_CMD_DIR_TX);

	/* Use default callback if none given */
	if (send_status == NULL) {
		send_status = self->itf_cbs.cmd_send_status;
		userdata = self->itf_cbs.userdata;
	}

	/* Determine queue where to put command */
	queue = find_tx_queue(self, cmd);
	if (queue == NULL)
		return -EINVAL;

	/* Add in tx queue */
	res = queue_add(queue, self, cmd, send_status, userdata);
	if (res < 0)
		return res;

	/* Check if something can be sent now */
	check_tx_queues(self);
	return 0;
}

static int should_process_data(struct arsdk_cmd_itf1 *self, uint8_t id,
		uint8_t seq)
{
	uint8_t prev = self->recv_seq[id];
	int diff = seq - prev;
	if ((diff > 0) /* newer */ ||
	    (diff < -10) /* loop */) {
		self->recv_seq[id] = seq;
		return 1;
	} else {
		return 0;
	}
}

/**
 */
static void lnqlt_rx_update(struct arsdk_cmd_itf1 *self,
		const struct arsdk_transport_header *header)
{
	int diff = header->seq - self->recv_seq[header->id];

	/* loop */
	if (diff < -10)
		diff += 256;

	if (diff > 0) {
		self->lnqlt.rx_miss_count += (diff-1);
		self->lnqlt.rx_useful_count++;
	} else {
		self->lnqlt.rx_useless_count++;
	}
}

/**
 */
int arsdk_cmd_itf1_recv_data(struct arsdk_cmd_itf1 *self,
		const struct arsdk_transport_header *header,
		const struct arsdk_transport_payload *payload)
{
	int res = 0;
	struct arsdk_cmd cmd;
	struct pomp_buffer *buf = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(header != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(payload != NULL, -EINVAL);

	if (self->transport == NULL)
		return -EPIPE;

	/* Update of the reception link quality */
	lnqlt_rx_update(self, header);

	/* Handle ACK frame and re-check tx queues */
	if (header->type == ARSDK_TRANSPORT_DATA_TYPE_ACK) {
		recv_ack(self, header, payload);
		check_tx_queues(self);
		/* Update last sequence number received */
		self->recv_seq[header->id] = header->seq;
		return 0;
	}

	/* Send ACK if needed */
	if (header->type == ARSDK_TRANSPORT_DATA_TYPE_WITHACK)
		send_ack(self, header->id, header->seq);

	/* If the sequence number was already handled, stop processing here */
	if (!should_process_data(self, header->id, header->seq))
		return 0;

	/* Initialize command with buffer of frame */
	if (payload->buf == NULL) {
		/* Frame has no buffer, but raw data, create a new one and
		 * init command with it, initial ref no more needed */
		buf = pomp_buffer_new_with_data(payload->cdata, payload->len);
		if (buf == NULL)
			return -ENOMEM;
		arsdk_cmd_init_with_buf(&cmd, buf);
		pomp_buffer_unref(buf);
	} else {
		arsdk_cmd_init_with_buf(&cmd, payload->buf);
	}

	/* Set arsdk_cmd buffer type from transport data type */
	switch (header->type) {
	case ARSDK_TRANSPORT_DATA_TYPE_NOACK:
		cmd.buffer_type = ARSDK_CMD_BUFFER_TYPE_NON_ACK;
	break;
	case ARSDK_TRANSPORT_DATA_TYPE_LOWLATENCY:
		cmd.buffer_type = ARSDK_CMD_BUFFER_TYPE_HIGH_PRIO;
	break;
	case ARSDK_TRANSPORT_DATA_TYPE_WITHACK:
		cmd.buffer_type = ARSDK_CMD_BUFFER_TYPE_ACK;
	break;
	case ARSDK_TRANSPORT_DATA_TYPE_ACK:
	case ARSDK_TRANSPORT_DATA_TYPE_UNKNOWN:
	default:
		cmd.buffer_type = ARSDK_CMD_BUFFER_TYPE_INVALID;
	break;
	}

	/* Try to decode header of command, Notify reception */
	res = arsdk_cmd_dec_header(&cmd);
	if (res < 0) {
		ARSDK_LOG_ERRNO("arsdk_cmd_dec_header", -res);
	} else {
		cmd_log(self, &cmd, ARSDK_CMD_DIR_RX);
		(*self->itf_cbs.recv_cmd)(self->itf, &cmd,
				self->itf_cbs.userdata);
	}

	/* Cleanup command */
	arsdk_cmd_clear(&cmd);
	return res;
}

/**
 */
int arsdk_cmd_itf1_new(struct arsdk_transport *transport,
		const struct arsdk_cmd_itf1_cbs *cbs,
		const struct arsdk_cmd_itf_cbs *itf_cbs,
		struct arsdk_cmd_itf *itf,
		const struct arsdk_cmd_queue_info *tx_info_table,
		uint32_t tx_count,
		uint8_t ackoff,
		struct arsdk_cmd_itf1 **ret_obj)
{
	int res = 0;
	uint32_t i = 0;
	struct arsdk_cmd_itf1 *self = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(transport != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->dispose != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(itf_cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(itf_cbs->recv_cmd != NULL, -EINVAL);

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Initialize structure */
	self->transport = transport;
	self->loop = arsdk_transport_get_loop(transport);
	self->cbs = *cbs;
	self->itf_cbs = *itf_cbs;
	self->itf = itf;
	self->ackoff = ackoff;

	/* Initialize recv_seq to a non-zero values in order to accept the
	   first data */
	memset(self->recv_seq, 0xff, sizeof(self->recv_seq));

	/* Create timer */
	self->timer = pomp_timer_new(self->loop, &timer_cb, self);
	if (self->timer == NULL) {
		res = -ENOMEM;
		goto error;
	}

	/* Create link quality timer */
	self->lnqlt.timer = pomp_timer_new(self->loop,
			&link_quality_timer_cb, self);
	if (self->lnqlt.timer == NULL) {
		res = -ENOMEM;
		goto error;
	}

	/* Start link quality callback */
	res = pomp_timer_set_periodic(self->lnqlt.timer,
			LINK_QUALITY_TIME_MS,
			LINK_QUALITY_TIME_MS);
	if (res < 0)
		goto error;

	/* Create tx queues */
	self->tx_queues = calloc(tx_count, sizeof(struct queue *));
	if (self->tx_queues == NULL) {
		res = -ENOMEM;
		goto error;
	}
	self->tx_count = tx_count;
	for (i = 0; i < tx_count; i++) {
		res = queue_new(&tx_info_table[i], &self->tx_queues[i]);
		if (res < 0)
			goto error;
	}

	*ret_obj = self;
	return 0;

	/* Cleanup in case of error */
error:
	arsdk_cmd_itf1_destroy(self);
	return res;
}

/**
 */
int arsdk_cmd_itf1_destroy(struct arsdk_cmd_itf1 *self)
{
	int res = 0;
	uint32_t i = 0;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	/* Stop queues */
	arsdk_cmd_itf1_stop(self);

	/* Notify command interface callback */
	if (self->cbs.dispose)
		(*self->cbs.dispose)(self, self->cbs.userdata);

	/* Free tx queues */
	if (self->tx_queues != NULL) {
		for (i = 0; i < self->tx_count; i++) {
			if (self->tx_queues[i] != NULL) {
				res = queue_destroy(self->tx_queues[i], self);
				if (res < 0)
					return res;
				self->tx_queues[i] = NULL;
			}
		}
		free(self->tx_queues);
	}

	/* Free timer */
	if (self->timer != NULL)
		pomp_timer_destroy(self->timer);

	/* Stop link quality timer */
	res = pomp_timer_clear(self->lnqlt.timer);
	if (res < 0)
		ARSDK_LOG_ERRNO("pomp_timer_clear", -res);
	/* Free link quality timer */
	if (self->lnqlt.timer != NULL)
		pomp_timer_destroy(self->lnqlt.timer);

	free(self);
	return 0;
}
