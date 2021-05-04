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

#ifndef _ARSDK_DESC_H_
#define _ARSDK_DESC_H_

enum arsdk_cmd_list_type {
	ARSDK_CMD_LIST_TYPE_NONE = 0,
	ARSDK_CMD_LIST_TYPE_LIST_ITEM,
	ARSDK_CMD_LIST_TYPE_MAP_ITEM,
};

enum arsdk_cmd_buffer_type {
	ARSDK_CMD_BUFFER_TYPE_INVALID = 0,
	ARSDK_CMD_BUFFER_TYPE_NON_ACK,
	ARSDK_CMD_BUFFER_TYPE_ACK,
	ARSDK_CMD_BUFFER_TYPE_HIGH_PRIO,
};

enum arsdk_cmd_timeout_policy {
	ARSDK_CMD_TIMEOUT_POLICY_POP = 0,
	ARSDK_CMD_TIMEOUT_POLICY_RETRY,
	ARSDK_CMD_TIMEOUT_POLICY_FLUSH,
};

enum arsdk_arg_type {
	ARSDK_ARG_TYPE_I8 = 0,
	ARSDK_ARG_TYPE_U8,
	ARSDK_ARG_TYPE_I16,
	ARSDK_ARG_TYPE_U16,
	ARSDK_ARG_TYPE_I32,
	ARSDK_ARG_TYPE_U32,
	ARSDK_ARG_TYPE_I64,
	ARSDK_ARG_TYPE_U64,
	ARSDK_ARG_TYPE_FLOAT,
	ARSDK_ARG_TYPE_DOUBLE,
	ARSDK_ARG_TYPE_STRING,
	ARSDK_ARG_TYPE_ENUM,
	ARSDK_ARG_TYPE_BINARY,
};

struct arsdk_enum_desc {
	const char  *name;
	int32_t     value;
};

struct arsdk_arg_desc {
	const char                    *name;
	enum arsdk_arg_type           type;

	const struct arsdk_enum_desc  *enum_desc_table;
	uint32_t                      enum_desc_count;
};

struct arsdk_cmd_desc {
	const char                     *name;
	uint8_t                        prj_id;
	uint8_t                        cls_id;
	uint16_t                       cmd_id;
	enum arsdk_cmd_list_type       list_type;
	enum arsdk_cmd_buffer_type     buffer_type;
	enum arsdk_cmd_timeout_policy  timeout_policy;

	const struct arsdk_arg_desc    *arg_desc_table;
	uint32_t                       arg_desc_count;
};

#endif /* _ARSDK_DESC_H_ */
