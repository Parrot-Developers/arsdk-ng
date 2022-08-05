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
#include "cmd_itf/arsdk_cmd_itf3.h"
#include "arsdk_default_log.h"

/** Link quality analysis frequency */
#define LINK_QUALITY_TIME_MS 5000
/** Command pack maximum size */
#define ARSDK_PACK_MAX_SIZE 1000

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
 * @param sent:         '1' to filter entries not completely sent.
 * @param cnt :         Counter value used to generate local variables names.
 */
#define queue_for_each_packed_entry__(queue, entry, sent, cnt) \
	uint32_t MNAME(i, cnt) = queue->head; \
	uint32_t MNAME(i_max, cnt) = queue->depth - 1; \
	uint32_t MNAME(i_end, cnt) = (queue->head + queue->pack.cmd_count) % \
			queue->depth; \
	if (sent && queue->pack.remaining_len > 0) \
		MNAME(i_end, cnt) = MNAME(i_end, cnt) > 0 ?\
			MNAME(i_end, cnt) - 1 : MNAME(i_max, cnt); \
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
		queue_for_each_packed_entry__(queue, entry, 0, __COUNTER__)

/**
 * Iterates over entries completely sent by the pack of the given queue.
 *
 * @param queue :       The queue to run through.
 * @param entry :       Itaration entry.
 */
#define queue_for_each_packed_entry_sent(queue, entry)\
		queue_for_each_packed_entry__(queue, entry, 1, __COUNTER__)

/** Queue entry */
struct entry {
	/** Command to send */
	struct arsdk_cmd                        cmd;
	/** Callback to notify the command sending status. */
	arsdk_cmd_itf_cmd_send_status_cb_t      cmd_send_status;
	/** User data given in callbacks */
	void                                    *userdata;
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
	/** Last sending time. */
	struct timespec              last_sent_ts;
	/** Command pack. */
	struct {
		/** Data buffer. */
		struct pomp_buffer      *buf;
		/** Number of command in the pack. */
		uint32_t                cmd_count;
		/** Remaining data length. */
		size_t                  remaining_len;
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
		/** Remaining data length. */
		size_t                  remaining_len;
	} last_pack;
};

/** Command interface version 3 */
struct arsdk_cmd_itf3 {
	/** Command interface V3 callbacks. */
	struct arsdk_cmd_itf3_cbs          cbs;
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

	/**
	 * Map of partial command received for each reception queue identifier.
	 */
	struct pomp_buffer *partial_cmd_buf[UINT8_MAX+1];

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
static void cmd_log(struct arsdk_cmd_itf3 *self,
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
		arsdk_cmd_itf_cmd_send_status_cb_t cmd_send_status,
		void *userdata)
{
	memset(entry, 0, sizeof(*entry));
	arsdk_cmd_copy(&entry->cmd, cmd);
	entry->cmd_send_status = cmd_send_status;
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

static enum arsdk_cmd_buffer_type data_type_to_buffer_type(
		enum arsdk_transport_data_type val, uint8_t queue_id)
{
	switch (val) {
	case ARSDK_TRANSPORT_DATA_TYPE_NOACK:
		return ARSDK_CMD_BUFFER_TYPE_NON_ACK;
	case ARSDK_TRANSPORT_DATA_TYPE_LOWLATENCY:
		return ARSDK_CMD_BUFFER_TYPE_HIGH_PRIO;
	case ARSDK_TRANSPORT_DATA_TYPE_WITHACK:
		return queue_id == ARSDK_TRANSPORT_ID_D2C_CMD_LOWPRIO
			? ARSDK_CMD_BUFFER_TYPE_LOW_PRIO
			: ARSDK_CMD_BUFFER_TYPE_ACK;
	case ARSDK_TRANSPORT_DATA_TYPE_ACK:
	case ARSDK_TRANSPORT_DATA_TYPE_UNKNOWN:
	default:
		return ARSDK_CMD_BUFFER_TYPE_INVALID;
	}

	return ARSDK_CMD_BUFFER_TYPE_INVALID;
}

/**
 */
static void entry_send_notify(struct entry *entry, struct arsdk_cmd_itf3 *self,
		enum arsdk_transport_data_type type, uint8_t queue_id,
		enum arsdk_cmd_itf_cmd_send_status send_status,
		uint16_t seq, int done)
{
	/* Notify cmd callback */
	if (entry->cmd_send_status != NULL) {
		(*entry->cmd_send_status)(self->itf, &entry->cmd,
				data_type_to_buffer_type(type, queue_id),
				send_status, seq, done,
				entry->userdata);
	}
}

/**
 */
static void pack_send_notify(struct arsdk_cmd_itf3 *self,
		uint16_t seq, enum arsdk_transport_data_type type,
		uint8_t queue_id, size_t len,
		enum arsdk_cmd_itf_pack_send_status send_status,
		uint32_t sent_count)
{
	/* Notify pack callback */
	if (self->itf_cbs.pack_send_status != NULL) {
		(*self->itf_cbs.pack_send_status)(self->itf, seq,
				data_type_to_buffer_type(type, queue_id), len,
				send_status, sent_count,
				self->itf_cbs.userdata);
	}
}

/**
 */
static void pack_recv_notify(struct arsdk_cmd_itf3 *self,
		uint16_t seq, enum arsdk_transport_data_type type,
		uint8_t queue_id, size_t len,
		enum arsdk_cmd_itf_pack_recv_status recv_status)
{
	/* Notify pack callback */
	if (self->itf_cbs.pack_recv_status != NULL) {
		(*self->itf_cbs.pack_recv_status)(self->itf, seq,
				data_type_to_buffer_type(type, queue_id), len,
				recv_status, self->itf_cbs.userdata);
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

	/* Force infinite retry, without overwriting. */
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
static void queue_stop(struct queue *queue, struct arsdk_cmd_itf3 *itf)
{
	uint32_t i = 0, pos = 0;
	struct entry *entry = NULL;

	/* Notify pack canceled */
	if (queue->pack.cmd_count != 0) {
		size_t len;
		pomp_buffer_get_cdata(queue->pack.buf, NULL, &len, NULL);

		pack_send_notify(itf, queue->pack.seq, queue->info.type,
				queue->info.id, len,
				ARSDK_CMD_ITF_PACK_SEND_STATUS_CANCELED, 0);
	}

	/* Cancel all entries of queue */
	pos = queue->head;
	for (i = 0; i < queue->count; i++) {
		entry = &queue->entries[pos];
		entry_send_notify(entry, itf, queue->info.type, queue->info.id,
				ARSDK_CMD_ITF_CMD_SEND_STATUS_CANCELED, 0, 1);
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
static int queue_destroy(struct queue *queue, struct arsdk_cmd_itf3 *itf)
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
		struct arsdk_cmd_itf3 *itf,
		const struct arsdk_cmd *cmd,
		arsdk_cmd_itf_cmd_send_status_cb_t cmd_send_status,
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
	entry_init(entry, cmd, cmd_send_status, userdata);
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
 * @param self : command interface.
 * @param queue : queue of commands to send.
 */
static void queue_pack_cmds(struct arsdk_cmd_itf3 *self, struct queue *queue)
{
	struct entry *entry = NULL;
	size_t pack_len = 0;

	queue_for_each_entry(queue, entry) {
		const uint8_t *cmd_data;
		size_t cmd_len;
		int done;

		pomp_buffer_get_cdata(entry->cmd.buf, (const void **)&cmd_data,
				&cmd_len, NULL);

		if (queue->pack.cmd_count == 0 &&
		    queue->last_pack.remaining_len != 0) {
			/* It is the following of a command partially sent. */
			cmd_data += cmd_len - queue->last_pack.remaining_len;
			cmd_len = queue->last_pack.remaining_len;
		} else {
			/* It is a new command. */

			/* Encode command size in varuint32 */
			uint8_t data[5];
			size_t data_size = 0;
			futils_varint_write_u32(data, sizeof(data),
					       cmd_len, &data_size);

			pack_len += data_size;
			/* Leave the loop if there is not enough space to write
			   the command size. */
			if (pack_len > ARSDK_PACK_MAX_SIZE)
				break;

			/* Append command size */
			pomp_buffer_append_data(queue->pack.buf,
						data, data_size);
		}

		/* Command data: */
		pack_len += cmd_len;
		if (pack_len > ARSDK_PACK_MAX_SIZE) {
			/* Command too large to fit in the pack */
			if (queue->info.type !=
					ARSDK_TRANSPORT_DATA_TYPE_WITHACK) {
				/* Only acknowledged commands could be sent
				   partially. */
				break;
			}

			queue->pack.remaining_len =
					pack_len - ARSDK_PACK_MAX_SIZE;
			cmd_len -= queue->pack.remaining_len;
		}

		/* Append command payload */
		pomp_buffer_append_data(queue->pack.buf, cmd_data, cmd_len);
		queue->pack.cmd_count++;

		/* Notify packed command */
		enum arsdk_cmd_itf_cmd_send_status status =
				queue->pack.remaining_len > 0 ?
				ARSDK_CMD_ITF_CMD_SEND_STATUS_PARTIALLY_PACKED :
				ARSDK_CMD_ITF_CMD_SEND_STATUS_PACKED;
		done = (queue->info.type == ARSDK_TRANSPORT_DATA_TYPE_NOACK &&
			status == ARSDK_CMD_ITF_CMD_SEND_STATUS_PACKED);
		entry_send_notify(entry,
				  self,
				  queue->info.type,
				  queue->info.id,
				  status,
				  queue->seq,
				  done);

		/* Leave the loop if there is remaining data. */
		if (queue->pack.remaining_len > 0)
			break;
	}
}

/**
 */
static struct queue *find_tx_queue(struct arsdk_cmd_itf3 *itf,
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
static void check_tx_queue(struct arsdk_cmd_itf3 *self,
		const struct timespec *tsnow,
		struct queue *queue,
		int *next_timeout_ms)
{
	int res = 0;
	uint64_t diff_us = 0;
	int diff_ms = 0;
	int remaining_ms = 0;
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

	/* If delay between tx is not passed, compute next time of check */
	if (queue->info.max_tx_rate_ms > 0 && time_timespec_diff_in_range(
			&queue->last_sent_ts,
			tsnow,
			(uint64_t)queue->info.max_tx_rate_ms * 1000,
			&diff_us)) {
		/* Still need to wait before sending */
		diff_ms = (int)(diff_us / 1000);
		remaining_ms = queue->info.max_tx_rate_ms - diff_ms;

		/* If the remaining time is less than a milisecond,
		   send the pack now. We should NEVER set next_timeout_ms
		   to zero here, as it would deactivate the pomp_timer
		*/
		if (remaining_ms > 0) {
			if (*next_timeout_ms < 0 ||
			    remaining_ms < *next_timeout_ms)
				*next_timeout_ms = remaining_ms;
			return;
		}
	}

	/* If it is not a retry, increment the sequence number and
	   pack new commands to send. */
	if (queue->pack.cmd_count == 0) {
		queue->seq++;
		queue_pack_cmds(self, queue);
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
	if (res < 0) {
		ARSDK_LOGI("arsdk_transport_send_data err: %d seq%" PRIu16
			   "queue: %" PRIu8, -res, queue->seq, queue->info.id);
		return;
	}

	/* Notify pack sent */
	pack_send_notify(self, header.seq, queue->info.type, queue->info.id,
			len, ARSDK_CMD_ITF_PACK_SEND_STATUS_SENT,
			queue->pack.sent_count + 1);
	if (queue->pack.sent_count == 100) {
		ARSDK_LOG_EVT("ARSDK",
			      "event='too_many_retries',max_pack_size=%d,current_pack_size=%zu",
			      ARSDK_PACK_MAX_SIZE, len);
	}
	queue->last_sent_ts = *tsnow;

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
static void check_tx_queues(struct arsdk_cmd_itf3 *self)
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
static void recv_ack(struct arsdk_cmd_itf3 *self,
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

			/* Notify pack ack received */
			pack_send_notify(self, seq, queue->info.type,
				queue->info.id, payload->len,
				ARSDK_CMD_ITF_PACK_SEND_STATUS_ACK_RECEIVED,
				queue->last_pack.ack_count);

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

			/* Notify pack ack received */
			pack_send_notify(self, seq, queue->info.type,
				queue->info.id, payload->len,
				ARSDK_CMD_ITF_PACK_SEND_STATUS_ACK_RECEIVED,
				0);
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

		/* Notify pack ack received */
		pack_send_notify(self, seq, queue->info.type, queue->info.id,
			payload->len,
			ARSDK_CMD_ITF_PACK_SEND_STATUS_ACK_RECEIVED, 1);

		/* notify and pop each command completely send of the pack */
		uint32_t cmd_count = queue->pack.remaining_len == 0 ?
						queue->pack.cmd_count :
						queue->pack.cmd_count - 1;
		for (entry_i = 0; entry_i < cmd_count; entry_i++) {
			entry = &queue->entries[queue->head];
			entry_send_notify(entry, self, queue->info.type,
				queue->info.id,
				ARSDK_CMD_ITF_CMD_SEND_STATUS_ACK_RECEIVED,
				seq, 1);
			queue_pop(queue);
		}

		/* Update last pack acknowledged. */
		queue->last_pack.seq = queue->seq;
		queue->last_pack.sent_count = queue->pack.sent_count;
		queue->last_pack.ack_count = 1;
		queue->last_pack.remaining_len = queue->pack.remaining_len;

		/* Reset pack */
		pomp_buffer_set_len(queue->pack.buf, 0);
		queue->pack.cmd_count = 0;
		queue->pack.sent_count = 0;
		queue->pack.remaining_len = 0;

		return;
	}

	ARSDK_LOGW("ACK: unknown id %u", id);
}

/**
 */
static int send_ack(struct arsdk_cmd_itf3 *self, uint8_t id, uint16_t seq)
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

	/* Notify pack acknowledge sent, force type to data with ack since
	 * we will never send an acknowledge for non-ack data */
	pack_recv_notify(self, seq, ARSDK_TRANSPORT_DATA_TYPE_WITHACK, id,
			sizeof(seq), ARSDK_CMD_ITF_PACK_RECV_STATUS_ACK_SENT);
	return res;
}

/**
 */
static void link_quality_timer_cb(struct pomp_timer *timer, void *userdata)
{
	struct arsdk_cmd_itf3 *self = userdata;
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
	struct arsdk_cmd_itf3 *self = userdata;
	check_tx_queues(self);
}

/**
 */
int arsdk_cmd_itf3_stop(struct arsdk_cmd_itf3 *self)
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
int arsdk_cmd_itf3_send(struct arsdk_cmd_itf3 *self,
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

static int should_process_data(struct arsdk_cmd_itf3 *self, uint8_t id,
		uint16_t seq)
{
	uint16_t prev = self->recv_seq[id];
	int diff = seq - prev;
	if ((diff > 0) /* newer */ ||
	    (diff < -10) /* loop */) {
		self->recv_seq[id] = seq;
		return 1;
	} else if (diff == 0) {
		ARSDK_LOGD("Duplicated seq num for queue: %" PRIu8
			   " ; recv: %" PRIu16 " prev: %" PRIu16,
				id, seq, prev);
		return 0;
	} else {
		ARSDK_LOGW("Bad seq num for queue: %" PRIu8 " ; recv: %" PRIu16
			   " prev: %" PRIu16, id, seq, prev);
		return 0;
	}
}

/**
 */
static void lnqlt_rx_update(struct arsdk_cmd_itf3 *self,
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
 * @param payload_data : payload data to unpack.
 * @param payload_len : payload length.
 * @return 0 in case of success, negative errno value in case of error.
 */
static int unpack_cmds(struct arsdk_cmd_itf3 *self,
		enum arsdk_transport_data_type data_type, uint8_t queue_id,
		const void *payload_data, size_t payload_len)
{
	int res = 0;
	struct arsdk_cmd cmd;
	size_t cmd_size = 0;
	size_t cmd_rcv_len = 0;
	size_t data_len;
	int is_partial_cmd;
	const uint8_t *data = payload_data;
	const uint8_t *data_end = data + payload_len;
	struct pomp_buffer *buf = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(data != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(payload_len > 0, -EINVAL);

	while (data < data_end) {
		data_len = data_end - data;
		if (data_type == ARSDK_TRANSPORT_DATA_TYPE_WITHACK &&
		    data == payload_data &&
		    self->partial_cmd_buf[queue_id] != NULL) {
			/* It is the following of a partial command. */
			buf = self->partial_cmd_buf[queue_id];
			self->partial_cmd_buf[queue_id] = NULL;

			res = pomp_buffer_get_cdata(buf, NULL, &cmd_rcv_len,
					&cmd_size);
			if (res < 0)
				return res;
		} else {
			/* It is a new command. */

			/* Read command size from varuint32 */
			uint32_t val;
			size_t val_len = 0;
			res = futils_varint_read_u32(data, data_len,
						     &val, &val_len);
			if (res < 0)
				return res;
			cmd_size = val;
			cmd_rcv_len = 0;
			data += val_len;
			data_len -= val_len;
		}

		is_partial_cmd = (cmd_size - cmd_rcv_len > data_len);
		if (is_partial_cmd) {
			/* It is an partial command */

			/* Only acknowledged commands could be sent partially */
			if (data_type != ARSDK_TRANSPORT_DATA_TYPE_WITHACK)
				return -EPROTO;

			data_len = data_end - data;
		} else {
			/* It is a complete command */
			data_len = cmd_size - cmd_rcv_len;
		}

		/* Create a new buffer if needed. */
		if (buf == NULL) {
			buf = pomp_buffer_new(cmd_size);
			if (buf == NULL)
				return -ENOMEM;
		}

		res = pomp_buffer_append_data(buf, data, data_len);
		if (res < 0) {
			pomp_buffer_unref(buf);
			buf = NULL;
			return res;
		}

		if (is_partial_cmd) {
			/* It is a partial command
			   save the buffer and wait for the continuation. */
			self->partial_cmd_buf[queue_id] = buf;
			return res;
		}

		data += data_len;
		arsdk_cmd_init_with_buf(&cmd, buf);

		/* Set arsdk_cmd buffer type from transport data type */
		cmd.buffer_type = data_type_to_buffer_type(data_type, queue_id);

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
int arsdk_cmd_itf3_recv_data(struct arsdk_cmd_itf3 *self,
		const struct arsdk_transport_header *header,
		const struct arsdk_transport_payload *payload)
{
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

	size_t len;
	const void *data = NULL;
	if (payload->cdata != NULL) {
		len = payload->len;
		data = payload->cdata;
	} else {
		/* Frame has no raw data, but buffer */
		pomp_buffer_get_cdata(payload->buf, &data, &len, NULL);
	}

	/* If the sequence number was already handled, stop processing here */
	if (!should_process_data(self, header->id, header->seq)) {
		/* Notify pack drop */
		pack_recv_notify(self, header->seq, header->type, header->id,
				len, ARSDK_CMD_ITF_PACK_RECV_STATUS_IGNORED);
		return 0;
	}

	/* Notify pack received */
	pack_recv_notify(self, header->seq, header->type, header->id, len,
			ARSDK_CMD_ITF_PACK_RECV_STATUS_PROCESSED);

	/* Unpack commands from the payload */
	return unpack_cmds(self, header->type, header->id, data, len);
}

/**
 */
int arsdk_cmd_itf3_new(struct arsdk_transport *transport,
		const struct arsdk_cmd_itf3_cbs *cbs,
		const struct arsdk_cmd_itf_cbs *itf_cbs,
		struct arsdk_cmd_itf *itf,
		const struct arsdk_cmd_queue_info *tx_info_table,
		uint32_t tx_count,
		uint8_t ackoff,
		struct arsdk_cmd_itf3 **ret_obj)
{
	int res = 0;
	uint32_t i = 0;
	struct arsdk_cmd_itf3 *self = NULL;

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
	arsdk_cmd_itf3_destroy(self);
	return res;
}

/**
 */
int arsdk_cmd_itf3_destroy(struct arsdk_cmd_itf3 *itf)
{
	int res = 0;
	uint32_t i = 0;

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	/* Stop queues */
	arsdk_cmd_itf3_stop(itf);

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
