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

#ifndef _ARSDK_CMD_ITF_H_
#define _ARSDK_CMD_ITF_H_

/**
 * Formats the full command identifier.
 *
 * @param prj_id : project identifier.
 * @param cls_id : class identifier.
 * @param cmd_id : command identifier.
 *
 * @return The full command identifier.
 */
#define ARSDK_CMD_FULL_ID(prj_id, cls_id, cmd_id) \
	((uint32_t)(prj_id) << 24 | \
	(uint32_t)(cls_id) << 16 | \
	(uint32_t)(cmd_id))

struct arsdk_binary {
	const void *cdata;
	uint32_t len;
};

/** Value structure */
struct arsdk_value {
	enum arsdk_arg_type type;   /**< value type */

	union {
		int8_t      i8;     /**< i8 value */
		uint8_t     u8;     /**< u8 value */
		int16_t     i16;    /**< i16 value */
		uint16_t    u16;    /**< u16 value */
		int32_t     i32;    /**< i32 value */
		uint32_t    u32;    /**< u32 value */
		uint64_t    u64;    /**< u64 value */
		int64_t     i64;    /**< i64 value */
		float       f32;    /**< f32 value */
		double      f64;    /**< f64 value */
		char        *str;   /**< str value */
		const char  *cstr;  /**< cstr value */
		struct arsdk_binary binary; /**< binary value */
	} data; /**< value data */
};

/** Command structure */
struct arsdk_cmd {
	uint8_t             prj_id;     /**< Project Id */
	uint8_t             cls_id;     /**< Class Id */
	uint16_t            cmd_id;     /**< Command Id */
	uint32_t            id;		/**< Full Id */
	struct pomp_buffer  *buf;       /**< Data buffer */
	void                *userdata;  /**< User data */
	enum arsdk_cmd_buffer_type buffer_type; /**< Buffer Type */
};

/**
 * Command direction
 */
enum arsdk_cmd_dir {
	ARSDK_CMD_DIR_RX,
	ARSDK_CMD_DIR_TX
};

/**
 * Command send status.
 */
enum arsdk_cmd_itf_send_status {
	ARSDK_CMD_ITF_SEND_STATUS_SENT,          /**< Sent on the network */
	ARSDK_CMD_ITF_SEND_STATUS_ACK_RECEIVED,  /**< Ack received */
	ARSDK_CMD_ITF_SEND_STATUS_TIMEOUT,       /**< No ack received */
	ARSDK_CMD_ITF_SEND_STATUS_CANCELED,      /**< Not sent */
};

/**
 * Function called to indicate status of sent commands.
 * @param itf : interface object.
 * @param cmd : command structure.
 * @param status : status of send operation.
 * @param done : indicates that frame is no longer used internally.
 * @param userdata : user data.
 *
 * @remarks the function can be called several times depending on internal
 * configuration (ex: SENT then ACK_RECEIVED). The flag done at 1 indicates
 * the last call.
 */
typedef void (*arsdk_cmd_itf_send_status_cb_t)(struct arsdk_cmd_itf *itf,
		const struct arsdk_cmd *cmd,
		enum arsdk_cmd_itf_send_status status,
		int done,
		void *userdata);

/**
 * Command interface callbacks.
 */
struct arsdk_cmd_itf_cbs {
	/** User data given in callbacks */
	void *userdata;

	/**
	 * Function called when command interface is going to be destroyed.
	 * All references to this interface must be removed.
	 * @param itf : interface object.
	 * @param userdata : user data.
	 */
	void (*dispose)(struct arsdk_cmd_itf *itf, void *userdata);

	/**
	 * Function called when a new command has been received.
	 * @param itf : interface object.
	 * @param cmd : command structure.
	 * @param userdata : user data.
	 */
	void (*recv_cmd)(struct arsdk_cmd_itf *itf,
			const struct arsdk_cmd *cmd,
			void *userdata);

	/**
	 * Log function called at the request to send a command and
	 * at the reception of a command.
	 * @param itf : interface object.
	 * @param dir : direction of the cmd.
	 * @param cmd : the command to log.
	 * @param userdata : user data.
	 */
	void (*cmd_log)(struct arsdk_cmd_itf *itf,
			enum arsdk_cmd_dir dir,
			const struct arsdk_cmd *cmd,
			void *userdata);

	/**
	 * Log function called when the transport layer sends/receives some
	 * data.
	 * @param itf : interface object.
	 * @param dir : direction of the cmd.
	 * @param cdata : the data sent/received.
	 * @param len : the size of cdata
	 * @param userdata : user data.
	 */
	void (*transport_log)(struct arsdk_cmd_itf *itf,
			enum arsdk_cmd_dir dir,
			const void *header,
			size_t headerlen,
			const void *payload,
			size_t payloadlen,
			void *userdata);

	/** Function called to indicate status of sent commands. */
	arsdk_cmd_itf_send_status_cb_t send_status;

	/**
	 * Function called to inform about the link quality.
	 * @param itf : interface object.
	 * @param tx_quality : transition quality in percent;
	 *                     -1 if not calculable.
	 * @param rx_quality : reception quality in percent;
	 *                     -1 if not calculable.
	 * @param rx_useful : reception useful in percent;
	 *                     -1 if not calculable.
	 * @param userdata : user data.
	 */
	void (*link_quality)(struct arsdk_cmd_itf *itf,
			int32_t tx_quality,
			int32_t rx_quality,
			int32_t rx_useful,
			void *userdata);
};

/**
 * Get the string description of a send status.
 * @param status : send status to convert.
 * @return string description of the send status.
 */
ARSDK_API const char *arsdk_cmd_itf_send_status_str(
		enum arsdk_cmd_itf_send_status val);

/**
 * Set os specific data associated with the interface.
 * @param itf : interface object.
 * @param osdata : os specific data.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_cmd_itf_set_osdata(struct arsdk_cmd_itf *itf, void *osdata);

/**
 * Get os specific data associated with the interface.
 * @param itf : interface object.
 * @return os specific data or NULL in case of error.
 */
ARSDK_API void *arsdk_cmd_itf_get_osdata(struct arsdk_cmd_itf *itf);

/**
 * Send a command.
 * @param itf : interface object.
 * @param cmd : command structure.
 * @param send_status : function to call with send status. If NULL, the one
 * given at creation will be used.
 * @param userdata : user data for send_status callback.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_cmd_itf_send(struct arsdk_cmd_itf *itf,
		const struct arsdk_cmd *cmd,
		arsdk_cmd_itf_send_status_cb_t send_status,
		void *userdata);

/**
 * Encode a command.
 * @param cmd : command structure to fill.
 * @param desc : description of command.
 * @param ... : arguments of commands.
 * @return 0 in case of success, negative errno value in case of error.
 *
 * @remarks this function is a generic one mainly used by the generated code.
 */
ARSDK_API int arsdk_cmd_enc(struct arsdk_cmd *cmd,
		const struct arsdk_cmd_desc *desc, ...);


/**
 * Encode a command.
 * @param cmd : command structure to fill.
 * @param desc : description of command.
 * @param argc : size of the argv array (must be equal to desc->arg_desc_count)
 * @param argv : arguments array (must contains exactly
 *               desc->arg_desc_count items with same types).
 * @return 0 in case of success, negative errno value in case of error.
 *
 */
ARSDK_API int arsdk_cmd_enc_argv(struct arsdk_cmd *cmd,
		const struct arsdk_cmd_desc *desc, size_t argc,
		const struct arsdk_value *argv);

/**
 * Decode a command.
 * @param cmd : command structure to decode.
 * @param desc : description of command.
 * @param ... : arguments of commands to fill.
 * @return 0 in case of success, negative errno value in case of error.
 *
 * @remarks this function is a generic one mainly used by the generated code.
 */
ARSDK_API int arsdk_cmd_dec(const struct arsdk_cmd *cmd,
		const struct arsdk_cmd_desc *desc, ...);

/**
 * Decode header of command (ids).
 * @param cmd : command structure to decode.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_cmd_dec_header(struct arsdk_cmd *cmd);

/**
 * Find the description of a command.
 * @param cmd : command structure with header (ids) already decoded.
 * @return description of the command or NULL in case of error.
 */
ARSDK_API const struct arsdk_cmd_desc *arsdk_cmd_find_desc(
		const struct arsdk_cmd *cmd);

/**
 * Format a command in a string for debug.
 * @param cmd : command structure to format.
 * @param buf : destination string.
 * @param len : maximum length of destination string (including NULL).
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_cmd_fmt(const struct arsdk_cmd *cmd, char *buf, size_t len);

/**
 * Get arg values from a cmd.
 * @param cmd : command structure containing the arg values.
 * @param values : array to receive the arg values.
 * @param max_count : size of the given values array.
 * @param count : this variable will contain the actual count of arg values.
 * @return 0 in case of success, negative errno value in case of error.
 */
ARSDK_API int arsdk_cmd_get_values(const struct arsdk_cmd *cmd,
		struct arsdk_value *values, size_t max_count, size_t *count);

/**
 * return a human string representation of an argument type.
 * @param val : the argument type.
 * @return the string representation of the argument.
 */
ARSDK_API const char *arsdk_arg_type_str(enum arsdk_arg_type val);

/**
 * Get the name of a command.
 * @param cmd : command structure with header (ids) already decoded.
 * @return name of the command or the string "Unknown" in case of error.
 */
static inline const char *arsdk_cmd_get_name(const struct arsdk_cmd *cmd)
{
	const struct arsdk_cmd_desc *desc = arsdk_cmd_find_desc(cmd);
	return desc == NULL ? "Unknown" : desc->name;
}

/**
 * Initialize a command structure.
 * @param cmd : command structure.
 */
static inline void arsdk_cmd_init(struct arsdk_cmd *cmd)
{
	if (!cmd)
		return;

	memset(cmd, 0, sizeof(*cmd));
	cmd->buffer_type = ARSDK_CMD_BUFFER_TYPE_INVALID;
}

/**
 * Initialize a command structure with a buffer.
 * @param cmd : command structure.
 * @param buf : buffer to used for the command.
 *
 * @remarks this will get an extra reference on the buffer.
 */
static inline void arsdk_cmd_init_with_buf(struct arsdk_cmd *cmd,
		struct pomp_buffer *buf)
{
	if (!cmd)
		return;

	arsdk_cmd_init(cmd);
	if (buf != NULL) {
		cmd->buf = buf;
		pomp_buffer_ref(buf);
	}
}

/**
 * Clear a command structure.
 * @param cmd : command structure.
 */
static inline void arsdk_cmd_clear(struct arsdk_cmd *cmd)
{
	if (!cmd)
		return;

	if (cmd->buf != NULL)
		pomp_buffer_unref(cmd->buf);
	arsdk_cmd_init(cmd);
}

/**
 * Copy a command structure.
 * @param dstcmd : destination command structure.
 * @param srccmd : source command structure.
 *
 * @remarks the internal buffer will simply get one extra reference.
 */
static inline void arsdk_cmd_copy(struct arsdk_cmd *dstcmd,
		const struct arsdk_cmd *srccmd)
{
	if (!dstcmd || !srccmd)
		return;

	memcpy(dstcmd, srccmd, sizeof(*dstcmd));
	if (dstcmd->buf != NULL)
		pomp_buffer_ref(dstcmd->buf);
}

#endif /* !_ARSDK_CMD_ITF_H_ */
