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

#include "arsdk_test.h"
#include "env/arsdk_test_env.h"

#define LOG_TAG "arsdk_test_cmd_itf"
#include "arsdk_test_log.h"

#define MIN_DATA_LEN 10
#define DATA_MIN 'A'
#define DATA_MAX 'Z'
#define idx_to_data(_i) (((_i) % (DATA_MAX - DATA_MIN)) + DATA_MIN)

struct test_cmd_info {
	struct arsdk_cmd_desc desc;

	size_t msg_size;
	size_t msg_cnt;

	size_t sent_cnt;
	size_t recv_cnt;
};

struct test_data {
	struct arsdk_test_env *env;

	struct {
		struct arsdk_cmd_itf *cmd_itf;
		struct arsdk_peer *peer;
	} dev;

	struct {
		struct arsdk_cmd_itf *cmd_itf;
		struct arsdk_device *device;
	} ctrl;

	struct test_cmd_info *cmds;
	size_t cmds_cnt;
};
static struct test_data s_data = {
};

struct arsdk_cmd_desc s_cmd_ack_desc1 = {
	.name = "cmd1",
	.prj_id = 1,
	.cls_id = 2,
	.cmd_id = 1,
	.list_type = ARSDK_CMD_LIST_TYPE_NONE,
	.buffer_type = ARSDK_CMD_BUFFER_TYPE_ACK,
	.timeout_policy = ARSDK_CMD_TIMEOUT_POLICY_RETRY,

	.arg_desc_table = (const struct arsdk_arg_desc[1]) {
		{
			"arg1",
			ARSDK_ARG_TYPE_STRING,

			NULL,
			0,
		}
	},
	.arg_desc_count = 1,
};

struct arsdk_cmd_desc s_cmd_ack_desc2 = {
	.name = "cmd2",
	.prj_id = 1,
	.cls_id = 2,
	.cmd_id = 2,
	.list_type = ARSDK_CMD_LIST_TYPE_NONE,
	.buffer_type = ARSDK_CMD_BUFFER_TYPE_ACK,
	.timeout_policy = ARSDK_CMD_TIMEOUT_POLICY_RETRY,

	.arg_desc_table = (const struct arsdk_arg_desc[1]) {
		{
			"arg1",
			ARSDK_ARG_TYPE_STRING,

			NULL,
			0,
		}
	},
	.arg_desc_count = 1,
};

struct arsdk_cmd_desc s_cmd_ack_desc3 = {
	.name = "cmd3",
	.prj_id = 1,
	.cls_id = 2,
	.cmd_id = 3,
	.list_type = ARSDK_CMD_LIST_TYPE_NONE,
	.buffer_type = ARSDK_CMD_BUFFER_TYPE_ACK,
	.timeout_policy = ARSDK_CMD_TIMEOUT_POLICY_RETRY,

	.arg_desc_table = (const struct arsdk_arg_desc[1]) {
		{
			"arg1",
			ARSDK_ARG_TYPE_STRING,

			NULL,
			0,
		}
	},
	.arg_desc_count = 1,
};

struct arsdk_cmd_desc s_cmd_lowprio_desc1 = {
	.name = "cmd4_lowprio",
	.prj_id = 1,
	.cls_id = 2,
	.cmd_id = 4,
	.list_type = ARSDK_CMD_LIST_TYPE_NONE,
	.buffer_type = ARSDK_CMD_BUFFER_TYPE_LOW_PRIO,
	.timeout_policy = ARSDK_CMD_TIMEOUT_POLICY_RETRY,

	.arg_desc_table = (const struct arsdk_arg_desc[1]) {
		{
			"arg1",
			ARSDK_ARG_TYPE_STRING,

			NULL,
			0,
		}
	},
	.arg_desc_count = 1,
};

static void send_status_cb (struct arsdk_cmd_itf *itf,
		const struct arsdk_cmd *cmd,
		enum arsdk_cmd_buffer_type type,
		enum arsdk_cmd_itf_cmd_send_status status,
		uint16_t seq,
		int done,
		void *userdata)
{
	TST_LOG("cmd %u,%u,%u: %s%s", cmd->prj_id, cmd->cls_id, cmd->cmd_id,
			arsdk_cmd_itf_cmd_send_status_str(status),
			done ? "(DONE)" : "");
}

static void send_cmd(struct arsdk_cmd_itf *cmd_itf,
		struct test_cmd_info *cmd_info)
{
	TST_LOG_FUNC();

	if (cmd_info->msg_size == 0)
		cmd_info->msg_size = MIN_DATA_LEN;

	/* format data */
	char *str = malloc(cmd_info->msg_size);
	CU_ASSERT_PTR_NOT_NULL_FATAL(str);
	size_t i;
	for (i = 0; i < cmd_info->msg_size - 1; i++) {
		str[i] = idx_to_data(i);
	}
	str[cmd_info->msg_size - 1] = '\0';

	struct arsdk_cmd cmd;
	arsdk_cmd_init(&cmd);

	int res = arsdk_cmd_enc(&cmd, &cmd_info->desc, str);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	res = arsdk_cmd_itf_send(cmd_itf, &cmd, &send_status_cb, &s_data);
	CU_ASSERT_EQUAL_FATAL(res, 0);
	cmd_info->sent_cnt++;

	arsdk_cmd_clear(&cmd);
	free(str);
}

static int send_cmds(struct arsdk_cmd_itf *cmd_itf)
{
	int sent_cnt = 0;
	size_t i;
	for (i = 0; i < s_data.cmds_cnt; i++) {
		if (s_data.cmds[i].msg_cnt - s_data.cmds[i].sent_cnt > 0) {
			send_cmd(s_data.dev.cmd_itf, &s_data.cmds[i]);
			sent_cnt++;
		}
	}

	return sent_cnt;
}

static void recv_cmd(const struct arsdk_cmd *cmd)
{
	CU_ASSERT_PTR_NOT_NULL_FATAL(cmd);

	struct test_cmd_info *cmd_info = NULL;
	size_t i;
	for (i = 0; i < s_data.cmds_cnt && !cmd_info; i++) {
		if (cmd->prj_id == s_data.cmds[i].desc.prj_id &&
		    cmd->cls_id == s_data.cmds[i].desc.cls_id &&
		    cmd->cmd_id == s_data.cmds[i].desc.cmd_id)
			cmd_info = &s_data.cmds[i];
	}
	CU_ASSERT_PTR_NOT_NULL_FATAL(cmd_info);

	cmd_info->recv_cnt++;

	const char *str = NULL;
	arsdk_cmd_dec(cmd, &cmd_info->desc, &str);
	CU_ASSERT_PTR_NOT_NULL_FATAL(str);

	size_t str_len = strlen(str) + 1;
	CU_ASSERT_EQUAL(str_len, cmd_info->msg_size);

	/* check data */
	for (i = 0; i < str_len - 1; i++) {
		CU_ASSERT_EQUAL(str[i], idx_to_data(i));
	}
	CU_ASSERT_EQUAL(str[str_len - 1], '\0');
}

/* CONTROLLER : */

/**
 */
static void ctrl_send_status(struct arsdk_cmd_itf *itf,
		const struct arsdk_cmd *cmd,
		enum arsdk_cmd_buffer_type type,
		enum arsdk_cmd_itf_cmd_send_status status,
		uint16_t seq,
		int done,
		void *userdata)
{
	TST_LOG("cmd %u,%u,%u: %s%s", cmd->prj_id, cmd->cls_id, cmd->cmd_id,
			arsdk_cmd_itf_cmd_send_status_str(status),
			done ? "(DONE)" : "");
}

/**
 */
static void ctrl_link_quality(struct arsdk_cmd_itf *itf,
		int32_t tx_quality,
		int32_t rx_quality,
		int32_t rx_useful,
		void *userdata)
{
	TST_LOG("link_quality tx_quality:%d%% rx_quality:%d%% rx_useful:%d%%",
			tx_quality, rx_quality, rx_useful);
}

/**
 */
static void ctrl_recv_cmd(struct arsdk_cmd_itf *itf,
		const struct arsdk_cmd *cmd,
		void *userdata)
{
	TST_LOG_FUNC();

	recv_cmd(cmd);

	int waiting_cmd = 0;
	size_t i;
	for (i = 0; i < s_data.cmds_cnt; i++) {
		waiting_cmd |= (s_data.cmds[i].recv_cnt < s_data.cmds[i].msg_cnt);
	}

	if (!waiting_cmd) {
		arsdk_test_env_loop_stop(s_data.env);
	} else {
		/* Device sends next commands. */
		send_cmds(s_data.dev.cmd_itf);
	}
}

static void ctrl_connected(struct arsdk_device *device,
			const struct arsdk_device_info *info,
			void *userdata)
{
	TST_LOG_FUNC();

	struct test_data *data = userdata;
	CU_ASSERT_PTR_NOT_NULL_FATAL(data);

	data->ctrl.device = device;

	/* Create command interface object */
	struct arsdk_cmd_itf_cbs cmd_cbs = {
		.userdata = data,
		.recv_cmd = &ctrl_recv_cmd,
		.cmd_send_status = &ctrl_send_status,
		.link_quality = &ctrl_link_quality,
	};
	int res = arsdk_device_create_cmd_itf(device, &cmd_cbs,
			&data->ctrl.cmd_itf);
	CU_ASSERT_EQUAL_FATAL(res, 0);
}

static void ctrl_disconnected(struct arsdk_device *device,
		const struct arsdk_device_info *info,
		void *userdata)
{
	TST_LOG_FUNC();

	struct test_data *data = userdata;
	CU_ASSERT_PTR_NOT_NULL_FATAL(data);

	data->ctrl.device = NULL;
}

/* DEVICE : */

/**
 */
static void dev_send_status(struct arsdk_cmd_itf *itf,
		const struct arsdk_cmd *cmd,
		enum arsdk_cmd_buffer_type type,
		enum arsdk_cmd_itf_cmd_send_status status,
		uint16_t seq,
		int done,
		void *userdata)
{
	TST_LOG("cmd %u,%u,%u: %s%s", cmd->prj_id, cmd->cls_id, cmd->cmd_id,
			arsdk_cmd_itf_cmd_send_status_str(status),
			done ? "(DONE)" : "");
}

/**
 */
static void dev_link_quality(struct arsdk_cmd_itf *itf, int32_t tx_quality,
		int32_t rx_quality, int32_t rx_useful, void *userdata)
{
	TST_LOG("link_quality tx_quality:%d%% rx_quality:%d%% rx_useful:%d%%",
			tx_quality, rx_quality, rx_useful);
}

/**
 */
static void dev_recv_cmd(struct arsdk_cmd_itf *itf,
		const struct arsdk_cmd *cmd,
		void *userdata)
{
	TST_LOG_FUNC();
}

static void dev_connected(struct arsdk_peer *peer,
			const struct arsdk_peer_info *info, void *userdata)
{
	TST_LOG_FUNC();

	struct test_data *data = userdata;
	CU_ASSERT_PTR_NOT_NULL_FATAL(data);

	CU_ASSERT_PTR_NOT_NULL_FATAL(peer);
	data->dev.peer = peer;

	/* Create command interface object */
	struct arsdk_cmd_itf_cbs cmd_cbs = {
		.userdata = data,
		.recv_cmd = &dev_recv_cmd,
		.cmd_send_status = &dev_send_status,
		.link_quality = &dev_link_quality,
	};

	int res = arsdk_peer_create_cmd_itf(peer, &cmd_cbs, &data->dev.cmd_itf);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	/* send msg */
	send_cmds(data->dev.cmd_itf);
}

static void dev_disconnected(struct arsdk_peer *peer,
		const struct arsdk_peer_info *info,
		void *userdata)
{
	TST_LOG_FUNC();

	struct test_data *data = userdata;
	CU_ASSERT_PTR_NOT_NULL_FATAL(data);

	CU_ASSERT_PTR_NOT_NULL_FATAL(peer);
	data->dev.peer = NULL;
}


/** */
static void test_run(enum arsdk_backend_type backend_type)
{
	int res;
	struct arsdk_test_env_cbs env_cbs = {
		.userdata = &s_data,

		.device_cbs = {
			.userdata = &s_data,
			.connected = &dev_connected,
			.disconnected = &dev_disconnected,
		},

		.ctrl_cbs = {
			.userdata = &s_data,
			.connected = &ctrl_connected,
			.disconnected = &ctrl_disconnected,
		},
	};

	TST_LOG_FUNC();

	res = arsdk_test_env_new(backend_type, &env_cbs, &s_data.env);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	res = arsdk_test_env_start(s_data.env);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	/* Run loop */
	res = arsdk_test_env_run_loop(s_data.env);
	CU_ASSERT_EQUAL_FATAL(res, 0);

	arsdk_test_env_stop(s_data.env);

	res = arsdk_test_env_destroy(s_data.env);
	CU_ASSERT_EQUAL_FATAL(res, 0);
	s_data.env = NULL;
}

/* net */

static void test_cmd_itf_net_large_ack_msg(void)
{
	TST_LOG("%s", __func__);

	memset(&s_data, 0, sizeof(s_data));

	struct test_cmd_info cmds[] = {
		{
			.desc = s_cmd_ack_desc1,

			.msg_size = 4 * 1024,
			.msg_cnt = 3,
		},
	};

	s_data.cmds = cmds;
	s_data.cmds_cnt = 1;

	test_run(ARSDK_BACKEND_TYPE_NET);

	/* checks */

	size_t i;
	for (i = 0; i < s_data.cmds_cnt; i++) {
		CU_ASSERT_EQUAL(s_data.cmds[i].sent_cnt, s_data.cmds[i].msg_cnt);
		CU_ASSERT_EQUAL(s_data.cmds[i].recv_cnt, s_data.cmds[i].msg_cnt);
	}
}

static void test_cmd_itf_net_multi_ack_msg(void)
{
	TST_LOG("%s", __func__);

	memset(&s_data, 0, sizeof(s_data));

	struct test_cmd_info cmds[] = {
		{
			.desc = s_cmd_ack_desc1,

			.msg_size = 20,
			.msg_cnt = 3,
		},

		{
			.desc = s_cmd_ack_desc2,

			.msg_size = 4 * 1024,
			.msg_cnt = 3,
		},

		{
			.desc = s_cmd_ack_desc3,

			.msg_size = 30,
			.msg_cnt = 3,
		},
	};

	s_data.cmds = cmds;
	s_data.cmds_cnt = 3;

	test_run(ARSDK_BACKEND_TYPE_NET);

	/* checks */

	size_t i;
	for (i = 0; i < s_data.cmds_cnt; i++) {
		CU_ASSERT_EQUAL(s_data.cmds[i].sent_cnt, s_data.cmds[i].msg_cnt);
		CU_ASSERT_EQUAL(s_data.cmds[i].recv_cnt, s_data.cmds[i].msg_cnt);
	}
}

static void test_cmd_itf_net_problematic_ack_msg(void)
{
	TST_LOG("%s", __func__);

	memset(&s_data, 0, sizeof(s_data));

	struct test_cmd_info cmds[] = {
		{
			.desc = s_cmd_ack_desc1,

			.msg_size = 1396,
			.msg_cnt = 3,
		},
	};

	s_data.cmds = cmds;
	s_data.cmds_cnt = 1;

	test_run(ARSDK_BACKEND_TYPE_NET);

	/* checks */

	size_t i;
	for (i = 0; i < s_data.cmds_cnt; i++) {
		CU_ASSERT_EQUAL(s_data.cmds[i].sent_cnt, s_data.cmds[i].msg_cnt);
		CU_ASSERT_EQUAL(s_data.cmds[i].recv_cnt, s_data.cmds[i].msg_cnt);
	}
}


static void test_cmd_itf_net_ack_lowprio_msg(void)
{
	TST_LOG("%s", __func__);

	memset(&s_data, 0, sizeof(s_data));

	struct test_cmd_info cmds[] = {
		{
			.desc = s_cmd_ack_desc1,

			.msg_size = 4 * 1024,
			.msg_cnt = 3,
		},

		{
			.desc = s_cmd_ack_desc2,

			.msg_size = 30,
			.msg_cnt = 3,
		},

		{
			.desc = s_cmd_lowprio_desc1,

			.msg_size = 4 * 1024,
			.msg_cnt = 3,
		},
	};

	s_data.cmds = cmds;
	s_data.cmds_cnt = 3;

	test_run(ARSDK_BACKEND_TYPE_NET);

	/* checks */

	size_t i;
	for (i = 0; i < s_data.cmds_cnt; i++) {
		CU_ASSERT_EQUAL(s_data.cmds[i].sent_cnt, s_data.cmds[i].msg_cnt);
		CU_ASSERT_EQUAL(s_data.cmds[i].recv_cnt, s_data.cmds[i].msg_cnt);
	}
}

/* mux */

static void test_cmd_itf_mux_large_ack_msg(void)
{
	TST_LOG("%s", __func__);

	memset(&s_data, 0, sizeof(s_data));

	struct test_cmd_info cmds[] = {
		{
			.desc = s_cmd_ack_desc1,

			.msg_size = 4 * 1024,
			.msg_cnt = 10,
		},
	};
	s_data.cmds = cmds;
	s_data.cmds_cnt = 1;

	test_run(ARSDK_BACKEND_TYPE_MUX);

	/* checks */

	size_t i;
	for (i = 0; i < s_data.cmds_cnt; i++) {
		CU_ASSERT_EQUAL(s_data.cmds[i].sent_cnt, s_data.cmds[i].msg_cnt);
		CU_ASSERT_EQUAL(s_data.cmds[i].recv_cnt, s_data.cmds[i].msg_cnt);
	}
}

static void test_cmd_itf_mux_multi_ack_msg(void)
{
	TST_LOG("%s", __func__);

	memset(&s_data, 0, sizeof(s_data));

	struct test_cmd_info cmds[] = {
		{
			.desc = s_cmd_ack_desc1,

			.msg_size = 20,
			.msg_cnt = 3,
		},

		{
			.desc = s_cmd_ack_desc2,

			.msg_size = 4 * 1024,
			.msg_cnt = 3,
		},

		{
			.desc = s_cmd_ack_desc3,

			.msg_size = 30,
			.msg_cnt = 3,
		},
	};

	s_data.cmds = cmds;
	s_data.cmds_cnt = 3;

	test_run(ARSDK_BACKEND_TYPE_MUX);

	/* checks */

	size_t i;
	for (i = 0; i < s_data.cmds_cnt; i++) {
		CU_ASSERT_EQUAL(s_data.cmds[i].sent_cnt, s_data.cmds[i].msg_cnt);
		CU_ASSERT_EQUAL(s_data.cmds[i].recv_cnt, s_data.cmds[i].msg_cnt);
	}
}

/* Disable some gcc warnings for test suite descriptions */
#ifdef __GNUC__
#  pragma GCC diagnostic ignored "-Wcast-qual"
#endif /* __GNUC__ */

/** */
static CU_TestInfo s_cmd_itf_tests[] = {
	{(char *)"cmd_itf_net_large_ack_msg", &test_cmd_itf_net_large_ack_msg},
	{(char *)"cmd_itf_net_multi_ack_msg", &test_cmd_itf_net_multi_ack_msg},
	{(char *)"cmd_itf_mux_large_ack_msg", &test_cmd_itf_mux_large_ack_msg},
	{(char *)"cmd_itf_mux_multi_ack_msg", &test_cmd_itf_mux_multi_ack_msg},
	{(char *)"cmd_itf_net_problematic_ack_msg", &test_cmd_itf_net_problematic_ack_msg},
	{(char *)"cmd_itf_net_ack_lowprio_msg", &test_cmd_itf_net_ack_lowprio_msg},
	CU_TEST_INFO_NULL,
};

/** */
/*extern*/ CU_SuiteInfo g_suites_cmd_itf[] = {
	{(char *)"cmd_itf", NULL, NULL, s_cmd_itf_tests},
	CU_SUITE_INFO_NULL,
};