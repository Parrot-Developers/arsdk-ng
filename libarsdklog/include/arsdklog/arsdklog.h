/**
 * Copyright (c) 2022 Parrot Drones SAS
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

#ifndef _ARSDKLOG_H_
#define _ARSDKLOG_H_

#include <arsdk/arsdk.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** To be used for all public API */
#ifdef ARSDKLOG_API_EXPORTS
#	ifdef _WIN32
#		define ARSDKLOG_API __declspec(dllexport)
#	else /* !_WIN32 */
#		define ARSDKLOG_API __attribute__((visibility("default")))
#	endif /* !_WIN32 */
#else /* !ARSDKLOG_API_EXPORTS */
#	define ARSDKLOG_API
#endif /* !ARSDKLOG_API_EXPORTS */

/* Event types */
enum arsdklog_event {
	/**
	 * Invalid event, should not be processed
	 */
	ARSDKLOG_EVENT_INVALID,

	/**
	 * A command is pushed to libarsdk.
	 * header fields:
	 * - type  : set to the command `enum arsdk_cmd_buffer_type' value
	 * - seq   : 0
	 * - count : 0
	 * - size  : 0
	 * payload:
	 * - Binary representation of the command
	 */
	ARSDKLOG_EVENT_CMD_PUSHED,

	/**
	 * A command is peeked from the queue and packed into a packet.
	 * header fields:
	 * - type  : set to the command `enum arsdk_cmd_buffer_type' value
	 * - seq   : set to the corresponding packet sequence number
	 * - count : 1 if the command is fully packed in the packet,
	 *           0 if the command is only partially packed and will require
	 *           more packets to be fully sent
	 * - size  : 0
	 * payload:
	 * - Binary representation of the command
	 */
	ARSDKLOG_EVENT_CMD_PACKED,

	/**
	 * A packet is sent.
	 * header fields:
	 * - type  : set to the packet `enum arsdk_cmd_buffer_type' value
	 * - seq   : set to the packet sequence number
	 * - count : number of tries for the current packet
	 *           (1 for the initial send, >1 for retries)
	 * - size  : size of the packet payload
	 * payload:
	 * - None
	 */
	ARSDKLOG_EVENT_PACK_SENT,

	/**
	 * An acknowledge is received.
	 * header fields:
	 * - type  : ARSDK_CMD_BUFFER_TYPE_ACK
	 * - seq   : set to the acknowledged packet sequence number
	 * - count : number of acknowledges for the packet
	 *           (1 for the initial ack, >1 for retries)
	 * - size  : size of the acknowledge payload
	 * payload:
	 * - None
	 */
	ARSDKLOG_EVENT_PACK_ACK_RECV,

	/**
	 * A packet is received.
	 * header fields:
	 * - type  : set to the packet `enum arsdk_cmd_buffer_type' value
	 * - seq   : set to the packet sequence number
	 * - count : 0 if the packet is processed,
	 *           non-zero if the packet is ignored
	 * - size  : size of the packet
	 * payload:
	 * - None
	 */
	ARSDKLOG_EVENT_PACK_RECV,

	/**
	 * An acknowledge is sent.
	 * header fields:
	 * - type  : ARSDK_CMD_BUFFER_TYPE_ACK
	 * - seq   : set to the acknowledged packet sequence number
	 * - count : 0
	 * - size  : size of the acknowledge payload
	 * payload:
	 * - None
	 */
	ARSDKLOG_EVENT_PACK_ACK_SENT,

	/**
	 * A command is acknowledged.
	 * header fields:
	 * - type  : set to the command `enum arsdk_cmd_buffer_type' value
	 * - seq   : set to the packet sequence number in which the last part
	 *           of the command was sent
	 * - count : 0
	 * - size  : 0
	 * payload:
	 * - Binary representation of the command
	 */
	ARSDKLOG_EVENT_CMD_ACK,

	/**
	 * A command is decoded and sent to the application.
	 * header fields:
	 * - type  : set to the command `enum arsdk_cmd_buffer_type' value
	 * - seq   : 0
	 * - count : 0
	 * - size  : 0
	 * payload:
	 * - Binary representation of the command
	 */
	ARSDKLOG_EVENT_CMD_POPPED,

	/**
	 * A packet send is aborted. This should only happen when closing
	 * the underlying libarsdk while commands are still in queue.
	 * header fields:
	 * - type  : set to the packet `enum arsdk_cmd_buffer_type' value
	 * - seq   : set to the packet sequence number
	 * - count : 0 if the packet is cancelled (library is closing),
	 *           1 if the packet has a timeout (older protocol versions)
	 * - size  : size of the packet
	 * payload:
	 * - None
	 */
	ARSDKLOG_EVENT_PACK_ABORTED,

	/**
	 * A command send is aborted. This should only happen when closing
	 * the underlying libarsdk while commands are still in queue.
	 * header fields:
	 * - type  : set to the command `enum arsdk_cmd_buffer_type' value
	 * - seq   : 0
	 * - count : 0 if the command is cancelled (library is closing),
	 *           1 if the command has a timeout (older protocol versions)
	 * - size  : 0
	 * payload:
	 * - Binary representation of the command
	 */
	ARSDKLOG_EVENT_CMD_ABORTED,
};


/**
 * Return structure for `arsdk_logger_log_event_parse()'
 */
struct arsdklog_evt_info {
	/**
	 * An event can be split into multiple chunks, events with chunk_id >0
	 * are continuation events
	 */
	int chunk_id;
	/**
	 * Event description
	 */
	enum arsdklog_event event;
	/**
	 * Instance_id of the source logger of the event
	 */
	uint32_t instance_id;
	/**
	 * See `enum arsdklog_event' for description of the
	 * `type', `seq', `count' and `size' fields
	 */
	enum arsdk_cmd_buffer_type type;
	uint32_t seq;
	uint32_t count;
	uint32_t size;
	/**
	 * Event payload pointer
	 */
	const char *payload;
	/**
	 * Event payload size
	 */
	size_t payload_size;
};


/** Forward declaration */
struct arsdk_logger;


/**
 * @brief Create a new arsdk logger for a given arsdk instance.
 *
 * @param ulog_device : ulog device for binary logging (NULL for
 * default).
 * @param instance_id : identifier of the instance. This ID is
 * added to the logs to allow a log reader to differentiate
 * between logs from different instances of libarsdk.
 * @param ret_logger : will receive the logger object.
 *
 * @return 0 in case of success, negative errno value in case of
 * error.
 */
ARSDKLOG_API int arsdk_logger_create(const char *ulog_device,
				     unsigned int instance_id,
				     struct arsdk_logger **ret_logger);


/**
 * @brief Closes & destroy an arsdk logger instance
 *
 * @param logger : the logger to destroy.
 *
 * @return 0 in case of success, negative errno value in case of
 * error.
 */
ARSDKLOG_API int arsdk_logger_destroy(struct arsdk_logger *logger);


/**
 * @brief Log CMD_PUSHED/CMD_POPPED events for a command.
 * This function is intended to be called from the `cmd_log' callback of the
 * cmd_itf object.
 *
 * @param logger : logger which should log the command.
 * @param dir : command direction.
 * @param cmd : the command to log.
 *
 * @return 0 in case of success, negative errno value in case of
 * error.
 */
ARSDKLOG_API int arsdk_logger_log_cmd(struct arsdk_logger *logger,
				      enum arsdk_cmd_dir dir,
				      const struct arsdk_cmd *cmd);


/**
 * @brief Log CMD_PACKED/CMD_ACK/CMD_ABORTED events for a command.
 * This function is intended to be called from the `cmd_send_status' callback of
 * the cmd_itf object.
 *
 * @param logger : logger which should log the command.
 * @param cmd : the command to log.
 * @param type : the buffer type of the command.
 * @param status : the current status of the command.
 * @param seq : the sequence number of the associated pack
 * (except for ABORTED)
 *
 * @return 0 in case of success, negative errno value in case of
 * error.
 */
ARSDKLOG_API int
arsdk_logger_log_cmd_send_status(struct arsdk_logger *logger,
				 const struct arsdk_cmd *cmd,
				 enum arsdk_cmd_buffer_type type,
				 enum arsdk_cmd_itf_cmd_send_status status,
				 uint16_t seq);


/**
 * @brief Log PACK_SENT/PACK_ACK_RECV/PACK_ABORTED events for a pack.
 * This function is intended to be called from the `pack_send_status' callback
 * of the cmd_itf object.
 *
 * @param logger : logger which should log the pack event.
 * @param seq : the pack sequence number.
 * @param type : the buffer type of the pack.
 * @param len : the size of the pack.
 * @param status : the current status of the pack.
 * @param count : (SENT/ACK only) the number of times the pack
 * was sent or acknowledged.
 *
 * @return 0 in case of success, negative errno value in case of
 * error.
 */
ARSDKLOG_API int
arsdk_logger_log_pack_send_status(struct arsdk_logger *logger,
				  int seq,
				  enum arsdk_cmd_buffer_type type,
				  size_t len,
				  enum arsdk_cmd_itf_pack_send_status status,
				  uint32_t count);


/**
 * @brief Log PACK_RECV/PACK_ACK_SENT events for a pack.
 * This function is intended to be called from the `pack_recv_status' callback
 * of the cmd_itf object.
 *
 * @param logger : logger which should log the pack event.
 * @param seq : the pack sequence number.
 * @param type : the buffer type of the pack.
 * @param len : the size of the pack.
 * @param status : the current status of the pack.
 *
 * @return 0 in case of success, negative errno value in case of
 * error.
 */
ARSDKLOG_API int
arsdk_logger_log_pack_recv_status(struct arsdk_logger *logger,
				  int seq,
				  enum arsdk_cmd_buffer_type type,
				  size_t len,
				  enum arsdk_cmd_itf_pack_recv_status status);


/**
 * @brief Parse a ulog event produced by this library.
 * This function is intended for log consumers.
 *
 * @param tag : the tag of the event.
 * @param payload : the payload of the event.
 * @param payload_size : the size of the payload.
 * @param info : the info structure filled by this call
 *
 * @return 0 in case of success, negative errno value in case of
 * error.
 */
ARSDKLOG_API int arsdk_logger_log_event_parse(const char *tag,
					      const char *payload,
					      size_t payload_size,
					      struct arsdklog_evt_info *info);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* !_ARSDKLOG_H_ */
