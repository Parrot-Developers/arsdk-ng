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
#include "cmd_itf/arsdk_cmd_itf2.h"
#include "arsdk_default_log.h"

/** Link quality analysis frequency */
#define LINK_QUALITY_TIME_MS 5000
/** Command pack maximum size */
#define ARSDK_PACK_MAX_SIZE 1400

/**
 * Formats a variable name to be used in a macro.
 *
 * @param base :        Base name.
 * @param cnt :         Counter value used to generate the name.
 *
 * @return name formatted as 'base''cnt'__
*/
#define MNAME(base, cnt) base ##cnt ##__

/**
 * Iterates over given queue entries.
 *
 * Should not be used directly, see queue_for_each_entry.
 *
 * @param queue :       The queue to run through.
 * @param entry :       Itaration entry.
 * @param cnt :         Counter value used to generate local variables names.
 */
#define queue_for_each_entry__(queue, entry, cnt) \
	uint32_t MNAME(i, cnt) = queue->head; \
	uint32_t MNAME(i_max, cnt) = queue->depth - 1; \
	entry = &queue->entries[MNAME(i, cnt)]; \
	for (; MNAME(i, cnt) != queue->tail; \
	     MNAME(i, cnt) = MNAME(i, cnt) < MNAME(i_max, cnt) ?\
			MNAME(i, cnt) + 1 : 0, \
	     entry = &queue->entries[MNAME(i, cnt)])

/**
 * Iterates over given queue entries.
 *
 * @param queue :       The queue to run through.
 * @param entry :       Itaration entry.
 */
#define queue_for_each_entry(queue, entry)\
		queue_for_each_entry__(queue, entry, __COUNTER__)

/**
 * Iterates over the packed entries of the given queue.
 *
 * Should not be used directly, see queue_for_each_packed_entry.
 *
 * @param queue :       The queue to run through.
 * @param entry :       Itaration entry.
 * @param cnt :         Counter value used to generate local variables names.
 */
#define queue_for_each_packed_entry__(queue, entry, cnt) \
	uint32_t MNAME(i, cnt) = queue->head; \
	uint32_t MNAME(i_max, cnt) = queue->depth - 1; \
	uint32_t MNAME(i_end, cnt) = (queue->head + queue->pack.cmd_count) % \
			queue->depth; \
	entry = &queue->entries[MNAME(i, cnt)]; \
	for (; MNAME(i, cnt) != MNAME(i_end, cnt); \
	     MNAME(i, cnt) = MNAME(i, cnt) < MNAME(i_max, cnt) ?\
			MNAME(i, cnt) + 1 : 0, \
	     entry = &queue->entries[MNAME(i, cnt)])

/**
 * Iterates over the packed entries of the given queue.
 *
 * @param queue :       The queue to run through.
 * @param entry :       Itaration entry.
 */
#define queue_for_each_packed_entry(queue, entry)\
		queue_for_each_packed_entry__(queue, entry, __COUNTER__)

/** Queue entry */
struct entry {
	/** Command to send */
	struct arsdk_cmd                    cmd;
	/** Callback to notify the command sending status. */
	arsdk_cmd_itf_cmd_send_status_cb_t  send_status;
	/** User data given in callbacks */
	void                                *userdata;
};

/** Sending Queue */
struct queue {
	/** Queue information. */
	struct arsdk_cmd_queue_info  info;
	/** Entries pending to be sent. */
	struct entry                 *entries;
	/** Number of pending entries. */
	uint32_t                     count;
	/** Queue depth. */
	uint32_t                     depth;
	/** Index of the oldest entry. */
	uint32_t                     head;
	/** Index where write a new entry. */
	uint32_t                     tail;
	/** Last sequence number used to send. */
	uint16_t                     seq;
	/** Command pack. */
	struct {
		/** Data buffer. */
		struct pomp_buffer      *buf;
		/** Number of command in the pack. */
		uint32_t                cmd_count;
		/** Sending sequence number. */
		uint16_t                seq;
		/** '1' if is waiting acknowledgement ; otherwise '0'. */
		int                     waiting_ack;
		/** Last sending time. */
		struct timespec         sent_ts;
		/** Sending count. */
		uint32_t                sent_count;
	} pack;
	/** Last pack acknowledged. */
	struct {
		/** Sending sequence number. */
		uint16_t                seq;
		/** Sending count. */
		uint32_t                sent_count;
		/** Count of acknowledgement received. */
		uint32_t                ack_count;
	} last_pack;
};

/** Command interface version 2 */
struct arsdk_cmd_itf2 {
	/** Command interface V2 callbacks. */
	struct arsdk_cmd_itf2_cbs          cbs;
	/** Command interface callbacks. */
	struct arsdk_cmd_itf_cbs           itf_cbs;
	/** Interface parent. */
	struct arsdk_cmd_itf               *itf;

	/** Transport used to send commands. */
	struct arsdk_transport             *transport;
	/** Pomp loop. */
	struct pomp_loop                   *loop;
	/** Retry timer. */
	struct pomp_timer                  *timer;
	/** Commands queues to send. */
	struct queue                       **tx_queues;
	/** Size of 'tx_queues'. */
	uint32_t                           tx_count;
	/**
	 * Index offset between a transmission queue and
	 * its reception acknowledge.
	 */
	uint8_t                            ackoff;
	/** Sequence number to used to send the next acknowledgement. */
	uint16_t                           next_ack_seq;
	/**
	 * Map of last sequence numbers received for each reception
	 * queue identifier.
	 */
	uint16_t                           recv_seq[UINT8_MAX+1];

	/** Link quality part. */
	struct {
		/** Link quality check timer. */
		struct pomp_timer          *timer;
		/** Count of retry counter; reset by the timer. */
		uint32_t                   retry_count;
		/** Count of acknowledgement received; reset by the timer. */
		uint32_t                   ack_count;
		/** Count of missed packets; reset by the timer. */
		uint32_t                   rx_miss_count;
		/** Count of useless packet received; reset by the timer. */
		uint32_t                   rx_useless_count;
		/** Count of useful packet received; reset by the timer. */
		uint32_t                   rx_useful_count;
	} lnqlt;
};

/**
 */
static void cmd_log(struct arsdk_cmd_itf2 *self,
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
		void *userdata)
{
	memset(entry, 0, sizeof(*entry));
	arsdk_cmd_copy(&entry->cmd, cmd);
	entry->send_status = send_status;
	entry->userdata = userdata;
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
static void entry_notify(struct entry *entry, struct arsdk_cmd_itf2 *self,
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
	int res;

	/* Allocate structure */
	queue = calloc(1, sizeof(*queue));
	if (queue == NULL)
		return -ENOMEM;

	/* Initialize structure */
	memcpy(&queue->info, info, sizeof(*info));

	/* Force infinite retry, as soon as possible without overwriting. */
	queue->info.max_tx_rate_ms = 0;
	queue->info.overwrite = 0;
	queue->info.default_max_retry_count = -1;

	/* Sequence number will wrap to 0 before sending first packet */
	queue->seq = UINT16_MAX;
	queue->last_pack.seq = UINT16_MAX;

	queue->pack.buf = pomp_buffer_new(ARSDK_PACK_MAX_SIZE);
	if (queue->pack.buf == NULL) {
		res = -ENOMEM;
		goto error;
	}

	*ret_queue = queue;
	return 0;
error:
	free(queue);
	return res;
}

/**
 */
static void queue_stop(struct queue *queue, struct arsdk_cmd_itf2 *itf)
{
	uint32_t i = 0, pos = 0;
	struct entry *entry = NULL;

	/* Cancel all entries of queue */
	pos = queue->head;
	for (i = 0; i < queue->count; i++) {
		entry = &queue->entries[pos];
		entry_notify(entry, itf, ARSDK_CMD_ITF_CMD_SEND_STATUS_CANCELED,
				1);
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
static int queue_destroy(struct queue *queue, struct arsdk_cmd_itf2 *itf)
{
	if (queue->count != 0)
		return -EBUSY;
	pomp_buffer_unref(queue->pack.buf);
	free(queue->entries);
	free(queue);
	return 0;
}

/**
 */
static int queue_add(struct queue *queue,
		struct arsdk_cmd_itf2 *itf,
		const struct arsdk_cmd *cmd,
		arsdk_cmd_itf_cmd_send_status_cb_t send_status,
		void *userdata)
{
	uint32_t newdepth = 0;
	struct entry *newentries = NULL;
	struct entry *entry = NULL;
	uint32_t cnt1 = 0, cnt2 = 0;

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
	entry_init(entry, cmd, send_status, userdata);
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
 * Packs as much as possible the pending commands in only one payload.
 *
 * @param queue: queue of commands to send.
 */
static void queue_pack_cmds(struct queue *queue)
{
	struct entry *entry = NULL;
	size_t pack_len = 0;

	queue_for_each_entry(queue, entry) {
		const void *cmd_data;
		size_t cmd_len;

		pomp_buffer_get_cdata(entry->cmd.buf, &cmd_data, &cmd_len,
				NULL);
		pack_len += cmd_len + sizeof(uint16_t);
		if (pack_len > ARSDK_PACK_MAX_SIZE) {
			/* command too large to fit in the pack */
			break;
		}

		/* Append command size in 16bits */
		pomp_buffer_append_data(queue->pack.buf,
					&cmd_len, sizeof(uint16_t));
		/* Append command payload */
		pomp_buffer_append_data(queue->pack.buf, cmd_data, cmd_len);
		queue->pack.cmd_count++;
	}
}

/**
 */
static struct queue *find_tx_queue(struct arsdk_cmd_itf2 *itf,
		const struct arsdk_cmd *cmd)
{
	uint32_t i = 0;
	const struct arsdk_cmd_desc *cmd_desc = NULL;
	struct queue *queue = NULL;
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
	for (i = 0; i < itf->tx_count; i++) {
		queue = itf->tx_queues[i];
		enum arsdk_transport_data_type qtype = queue->info.type;
		uint8_t qid = queue->info.id;
		switch (buffer_type) {
		case ARSDK_CMD_BUFFER_TYPE_NON_ACK:
			if (qtype == ARSDK_TRANSPORT_DATA_TYPE_NOACK)
				return queue;
			break;

		case ARSDK_CMD_BUFFER_TYPE_ACK:
		case ARSDK_CMD_BUFFER_TYPE_HIGH_PRIO:
			if (qtype == ARSDK_TRANSPORT_DATA_TYPE_WITHACK &&
			    qid != ARSDK_TRANSPORT_ID_D2C_CMD_LOWPRIO)
				return queue;
			break;

		case ARSDK_CMD_BUFFER_TYPE_LOW_PRIO:
			if (qtype == ARSDK_TRANSPORT_DATA_TYPE_WITHACK &&
			    qid == ARSDK_TRANSPORT_ID_D2C_CMD_LOWPRIO)
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
static void check_tx_queue(struct arsdk_cmd_itf2 *self,
		const struct timespec *tsnow,
		struct queue *queue,
		int *next_timeout_ms)
{
	int res = 0;
	uint64_t diff_us = 0;
	int diff_ms = 0;
	int remaining_ms = 0;
	struct entry *entry = NULL;
	struct arsdk_transport_header header;
	struct arsdk_transport_payload payload;
	size_t len = 0;
	uint32_t i = 0;

again:

	/* Nothing to do if queue is empty */
	if (queue->count == 0)
		return;

	/* If waiting for an ack, compute next time of check */
	if (queue->pack.waiting_ack) {
		if (time_timespec_diff_in_range(
				&queue->pack.sent_ts,
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

		/* Retry sending command */
		queue->pack.waiting_ack = 0;
		memset(&queue->pack.sent_ts, 0, sizeof(queue->pack.sent_ts));
		self->lnqlt.retry_count++;
	}

	/* If it is not a retry, increment the sequence number and
	   pack new commands to send. */
	if (queue->pack.cmd_count == 0) {
		queue->seq++;
		queue_pack_cmds(queue);
	}

	/* Determine pack buffer length */
	pomp_buffer_get_cdata(queue->pack.buf, NULL, &len, NULL);

	/* Construct header and payload */
	memset(&header, 0, sizeof(header));
	header.type = queue->info.type;
	header.id = queue->info.id;
	header.seq = queue->seq;

	arsdk_transport_payload_init_with_buf(&payload, queue->pack.buf);

	/* Send it */
	res = arsdk_transport_send_data(self->transport, &header, &payload,
			NULL, 0);
	arsdk_transport_payload_clear(&payload);
	if (res < 0)
		return;

	/* notify each command sent in the pack */
	queue_for_each_packed_entry(queue, entry) {
		entry_notify(entry, self, ARSDK_CMD_ITF_CMD_SEND_STATUS_PACKED,
				queue->info.type !=
				ARSDK_TRANSPORT_DATA_TYPE_WITHACK);
	}
	if (queue->info.type == ARSDK_TRANSPORT_DATA_TYPE_WITHACK) {
		queue->pack.seq = header.seq;
		queue->pack.waiting_ack = 1;
		queue->pack.sent_ts = *tsnow;
		queue->pack.sent_count++;
		/* update ack timeout */
		if (queue->info.ack_timeout_ms > 0) {
			diff_ms = queue->info.ack_timeout_ms;
			if (*next_timeout_ms < 0 || diff_ms < *next_timeout_ms)
				*next_timeout_ms = diff_ms;
		}
	} else {
		/* pop all commands send in the pack */
		for (i = 0; i < queue->pack.cmd_count; i++)
			queue_pop(queue);

		/* reset pack */
		pomp_buffer_set_len(queue->pack.buf, 0);
		queue->pack.cmd_count = 0;

		goto again;
	}
}

/**
 */
static void check_tx_queues(struct arsdk_cmd_itf2 *self)
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
static void recv_ack(struct arsdk_cmd_itf2 *self,
		const struct arsdk_transport_header *header,
		const struct arsdk_transport_payload *payload)
{
	uint16_t seq = 0, id = 0;
	uint32_t queue_i = 0;
	struct queue *queue = NULL;
	struct entry *entry = NULL;
	uint32_t entry_i = 0;

	if (payload->cdata == NULL) {
		ARSDK_LOGW("ACK: missing seq");
		return;
	}
	if (payload->len < sizeof(seq)) {
		ARSDK_LOGW("ACK: missing seq");
		return;
	}
	memcpy(&seq, payload->cdata, sizeof(seq));
	id = header->id - self->ackoff;

	for (queue_i = 0; queue_i < self->tx_count; queue_i++) {
		queue = self->tx_queues[queue_i];
		if (queue->info.id != id)
			continue;

		if (seq == queue->last_pack.seq) {
			/* Acknowledgement of a last pack retry. */
			queue->last_pack.ack_count++;
			ARSDK_LOGD("ACK: id(%u) seq(%u) ack(%u/%u)",
					id, seq,
					queue->last_pack.ack_count,
					queue->last_pack.sent_count);

			if (queue->last_pack.ack_count >
					queue->last_pack.sent_count) {
				ARSDK_LOGE("ACK: id(%u) seq(%u) "
					   "More ack(%u) than sendings(%u)",
						id, seq,
						queue->last_pack.ack_count,
						queue->last_pack.sent_count);
			}

			return;
		} else if (seq != queue->seq) {
			ARSDK_LOGE("ACK: Bad seq for id %u (%d/%d)",
					id, seq, queue->seq);
			return;
		}

		if (!queue->pack.waiting_ack) {
			ARSDK_LOGE("ACK: no ack waited for id %u", id);
			return;
		}

		if (queue->count == 0) {
			ARSDK_LOGE("ACK: no pending pack for id %u", id);
			return;
		}

		self->lnqlt.ack_count++;
		/* notify and pop each command of the pack */
		for (entry_i = 0; entry_i < queue->pack.cmd_count; entry_i++) {
			entry = &queue->entries[queue->head];
			entry_notify(entry, self,
				     ARSDK_CMD_ITF_CMD_SEND_STATUS_ACK_RECEIVED,
				     1);
			queue_pop(queue);
		}

		/* Update last pack acknowledged. */
		queue->last_pack.seq = queue->seq;
		queue->last_pack.sent_count = queue->pack.sent_count;
		queue->last_pack.ack_count = 1;

		/* reset pack */
		pomp_buffer_set_len(queue->pack.buf, 0);
		queue->pack.cmd_count = 0;
		queue->pack.sent_count = 0;

		return;
	}

	ARSDK_LOGW("ACK: unknown id %u", id);
}

/**
 */
static int send_ack(struct arsdk_cmd_itf2 *self, uint8_t id, uint16_t seq)
{
	int res = 0;
	struct arsdk_transport_header header;
	struct arsdk_transport_payload payload;

	/* Construct data with given frame's seq as data */
	memset(&header, 0, sizeof(header));
	header.type = ARSDK_TRANSPORT_DATA_TYPE_ACK;
	header.id = id + self->ackoff;
	header.seq = self->next_ack_seq++;
	arsdk_transport_payload_init_with_data(&payload, &seq, sizeof(seq));

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
	struct arsdk_cmd_itf2 *self = userdata;
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
				rx_useful, self->cbs.userdata);

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
	struct arsdk_cmd_itf2 *self = userdata;
	check_tx_queues(self);
}

/**
 */
int arsdk_cmd_itf2_stop(struct arsdk_cmd_itf2 *self)
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
int arsdk_cmd_itf2_send(struct arsdk_cmd_itf2 *self,
		const struct arsdk_cmd *cmd,
		arsdk_cmd_itf_cmd_send_status_cb_t send_status,
		void *userdata)
{
	int res = 0;
	struct queue *queue = NULL;

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

static int should_process_data(struct arsdk_cmd_itf2 *self, uint8_t id,
		uint16_t seq)
{
	uint16_t prev = self->recv_seq[id];
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
static void lnqlt_rx_update(struct arsdk_cmd_itf2 *self,
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
 * Unpacks each command from the playload.
 *
 * @param self : command interface.
 * @param data_type : transport data type of the queue.
 * @param queue_id : id of the queue.
 * @param frame_data : frame data to unpack.
 * @param len : lrame data length.
 * @return 0 in case of success, negative errno value in case of error.
 */
static int unpack_cmds(struct arsdk_cmd_itf2 *self,
		enum arsdk_transport_data_type data_type, uint8_t queue_id,
		const void *frame_data, size_t frame_data_len)
{
	int res = 0;
	struct arsdk_cmd cmd;
	uint16_t cmd_size;
	const uint8_t *data = frame_data;
	const uint8_t *data_end = data + frame_data_len;
	struct pomp_buffer *buf = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(data != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(frame_data_len > 0, -EINVAL);

	while (data + sizeof(uint16_t) <= data_end) {
		/* Read command size in 16bits */
		cmd_size = ARSDK_LE16TOH(*((uint16_t *)data));
		data += sizeof(uint16_t);
		if (data + cmd_size > data_end)
			return -EPROTO;

		buf = pomp_buffer_new_with_data(data, cmd_size);
		if (buf == NULL)
			return -ENOMEM;

		data += cmd_size;
		arsdk_cmd_init_with_buf(&cmd, buf);

		/* Set arsdk_cmd buffer type from transport data type */
		switch (data_type) {
		case ARSDK_TRANSPORT_DATA_TYPE_NOACK:
			cmd.buffer_type = ARSDK_CMD_BUFFER_TYPE_NON_ACK;
			break;
		case ARSDK_TRANSPORT_DATA_TYPE_LOWLATENCY:
			cmd.buffer_type = ARSDK_CMD_BUFFER_TYPE_HIGH_PRIO;
			break;
		case ARSDK_TRANSPORT_DATA_TYPE_WITHACK:
			cmd.buffer_type =
				queue_id == ARSDK_TRANSPORT_ID_D2C_CMD_LOWPRIO
					? ARSDK_CMD_BUFFER_TYPE_LOW_PRIO
					: ARSDK_CMD_BUFFER_TYPE_ACK;
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

		/* Cleanup */
		arsdk_cmd_clear(&cmd);
		pomp_buffer_unref(buf);
		buf = NULL;
	}

	return res;
}


/**
 */
int arsdk_cmd_itf2_recv_data(struct arsdk_cmd_itf2 *self,
		const struct arsdk_transport_header *header,
		const struct arsdk_transport_payload *payload)
{
	int res = 0;

	ARSDK_RETURN_ERR_IF_FAILED(header != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(payload != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

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


	/* Unpack commands from the payload */
	if (payload->cdata != NULL) {
		res = unpack_cmds(self, header->type, header->id,
				payload->cdata, payload->len);
	} else {
		/* Frame has no raw data, but buffer */
		size_t len;
		const void *data = NULL;

		pomp_buffer_get_cdata(payload->buf, &data, &len, NULL);
		res = unpack_cmds(self, header->type, header->id, data, len);
	}

	return res;
}

/**
 */
int arsdk_cmd_itf2_new(struct arsdk_transport *transport,
		const struct arsdk_cmd_itf2_cbs *cbs,
		const struct arsdk_cmd_itf_cbs *itf_cbs,
		struct arsdk_cmd_itf *itf,
		const struct arsdk_cmd_queue_info *tx_info_table,
		uint32_t tx_count,
		uint8_t ackoff,
		struct arsdk_cmd_itf2 **ret_obj)
{
	int res = 0;
	uint32_t i = 0;
	struct arsdk_cmd_itf2 *self = NULL;

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
	arsdk_cmd_itf2_destroy(self);
	return res;
}

/**
 */
int arsdk_cmd_itf2_destroy(struct arsdk_cmd_itf2 *itf)
{
	int res = 0;
	uint32_t i = 0;

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	/* Stop queues */
	arsdk_cmd_itf2_stop(itf);

	/* Notify command interface callback */
	(*itf->cbs.dispose)(itf, itf->cbs.userdata);

	/* Free tx queues */
	if (itf->tx_queues != NULL) {
		for (i = 0; i < itf->tx_count; i++) {
			if (itf->tx_queues[i] != NULL) {
				res = queue_destroy(itf->tx_queues[i], itf);
				if (res < 0)
					return res;
				itf->tx_queues[i] = NULL;
			}
		}
		free(itf->tx_queues);
	}

	/* Free timer */
	if (itf->timer != NULL)
		pomp_timer_destroy(itf->timer);

	/* Stop link quality timer */
	res = pomp_timer_clear(itf->lnqlt.timer);
	if (res < 0)
		ARSDK_LOG_ERRNO("pomp_timer_clear", -res);
	/* Free link quality timer */
	if (itf->lnqlt.timer != NULL)
		pomp_timer_destroy(itf->lnqlt.timer);

	free(itf);
	return 0;
}
