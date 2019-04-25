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
#include <libpomp.h>
#include <arsdk/arsdk.h>
#include <arsdk/internal/arsdk_internal.h>
#include <float.h>

static const struct arsdk_arg_desc s_arg_desc_i8 = {
	"i8",
	ARSDK_ARG_TYPE_I8,
	NULL,
	0,
};

static const struct arsdk_arg_desc s_arg_desc_u8 = {
	"u8",
	ARSDK_ARG_TYPE_U8,
	NULL,
	0,
};

static const struct arsdk_arg_desc s_arg_desc_i16 = {
	"i16",
	ARSDK_ARG_TYPE_I16,
	NULL,
	0,
};

static const struct arsdk_arg_desc s_arg_desc_u16 = {
	"u16",
	ARSDK_ARG_TYPE_U16,
	NULL,
	0,
};

static const struct arsdk_arg_desc s_arg_desc_i32 = {
	"i32",
	ARSDK_ARG_TYPE_I32,
	NULL,
	0,
};

static const struct arsdk_arg_desc s_arg_desc_u32 = {
	"u32",
	ARSDK_ARG_TYPE_U32,
	NULL,
	0,
};

static const struct arsdk_arg_desc s_arg_desc_i64 = {
	"i64",
	ARSDK_ARG_TYPE_I64,
	NULL,
	0,
};

static const struct arsdk_arg_desc s_arg_desc_u64 = {
	"u64",
	ARSDK_ARG_TYPE_U64,
	NULL,
	0,
};

static const struct arsdk_arg_desc s_arg_desc_float = {
	"float",
	ARSDK_ARG_TYPE_FLOAT,
	NULL,
	0,
};

static const struct arsdk_arg_desc s_arg_desc_double = {
	"double",
	ARSDK_ARG_TYPE_DOUBLE,
	NULL,
	0,
};

static const struct arsdk_arg_desc s_arg_desc_string = {
	"string",
	ARSDK_ARG_TYPE_STRING,
	NULL,
	0,
};

enum {
	TEST_ENUM_TYPE_A = 0,
	TEST_ENUM_TYPE_B = 1,
};
#define TEST_ENUM_TYPE_COUNT 2

static const struct arsdk_enum_desc s_enum_desc[] = {
	{"A", TEST_ENUM_TYPE_A},
	{"B", TEST_ENUM_TYPE_B},
};

static const struct arsdk_arg_desc s_arg_desc_enum = {
	"enum",
	ARSDK_ARG_TYPE_ENUM,
	s_enum_desc,
	sizeof(s_enum_desc) / sizeof(s_enum_desc[0]),
};


/** */
static void test_enc_dec_void(void)
{
	int res = 0;
	struct arsdk_cmd cmd;

	struct arsdk_cmd_desc cmd_void = {
		"cmd_void",
		UINT8_MAX,
		UINT8_MAX,
		UINT16_MAX,
		ARSDK_CMD_LIST_TYPE_NONE,
		ARSDK_CMD_BUFFER_TYPE_NON_ACK,
		ARSDK_CMD_TIMEOUT_POLICY_POP,
		NULL,
		0,
	};

	/* test */
	res = arsdk_cmd_enc(&cmd, &cmd_void);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_void);
	CU_ASSERT_EQUAL(res, 0);
}

/** */
static void test_enc_dec_single_i8(void)
{
	int res = 0;
	struct arsdk_cmd cmd;

	const struct arsdk_arg_desc arg_single_i8[] = {
		s_arg_desc_i8,
	};
	int8_t i8enc = INT8_MIN;
	int8_t i8dec = 0;

	struct arsdk_cmd_desc cmd_single_i8 = {
		"cmd_i8",
		UINT8_MAX,
		UINT8_MAX,
		UINT16_MAX,
		ARSDK_CMD_LIST_TYPE_NONE,
		ARSDK_CMD_BUFFER_TYPE_NON_ACK,
		ARSDK_CMD_TIMEOUT_POLICY_POP,
		arg_single_i8,
		sizeof(arg_single_i8) / sizeof(arg_single_i8[0]),
	};

	/* test min */
	res = arsdk_cmd_enc(&cmd, &cmd_single_i8, i8enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_i8, &i8dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(i8dec, i8enc);

	/* test max */
	i8enc = INT8_MAX;
	res = arsdk_cmd_enc(&cmd, &cmd_single_i8, i8enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_i8, &i8dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(i8dec, INT8_MAX);

	/* test 0 */
	i8enc = 0;
	res = arsdk_cmd_enc(&cmd, &cmd_single_i8, i8enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_i8, &i8dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(i8dec, 0);

	/* test -1 */
	i8enc = -1;
	res = arsdk_cmd_enc(&cmd, &cmd_single_i8, i8enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_i8, &i8dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(i8dec, -1);
}

/** */
static void test_enc_dec_single_u8(void)
{
	int res = 0;
	struct arsdk_cmd cmd;

	const struct arsdk_arg_desc arg_single_u8[] = {
		s_arg_desc_u8,
	};
	uint8_t u8enc = 0;
	uint8_t u8dec = 0;

	struct arsdk_cmd_desc cmd_single_u8 = {
		"cmd_u8",
		UINT8_MAX,
		UINT8_MAX,
		UINT16_MAX,
		ARSDK_CMD_LIST_TYPE_NONE,
		ARSDK_CMD_BUFFER_TYPE_NON_ACK,
		ARSDK_CMD_TIMEOUT_POLICY_POP,
		arg_single_u8,
		sizeof(arg_single_u8) / sizeof(arg_single_u8[0]),
	};

	/* test 0 */
	res = arsdk_cmd_enc(&cmd, &cmd_single_u8, u8enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_u8, &u8dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(u8dec, u8enc);

	/* test max */
	u8enc = UINT8_MAX;
	res = arsdk_cmd_enc(&cmd, &cmd_single_u8, u8enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_u8, &u8dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(u8dec, UINT8_MAX);
}

/** */
static void test_enc_dec_single_i16(void)
{
	int res = 0;
	struct arsdk_cmd cmd;

	const struct arsdk_arg_desc arg_single_i16[] = {
		s_arg_desc_i16,
	};
	int16_t i16enc = INT16_MIN;
	int16_t i16dec = 0;

	struct arsdk_cmd_desc cmd_single_i16 = {
		"cmd_i16",
		UINT8_MAX,
		UINT8_MAX,
		UINT16_MAX,
		ARSDK_CMD_LIST_TYPE_NONE,
		ARSDK_CMD_BUFFER_TYPE_NON_ACK,
		ARSDK_CMD_TIMEOUT_POLICY_POP,
		arg_single_i16,
		sizeof(arg_single_i16) / sizeof(arg_single_i16[0]),
	};

	/* test min*/
	res = arsdk_cmd_enc(&cmd, &cmd_single_i16, i16enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_i16, &i16dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(i16dec, INT16_MIN);

	/* test max */
	i16enc = INT16_MAX;
	res = arsdk_cmd_enc(&cmd, &cmd_single_i16, i16enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_i16, &i16dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(i16dec, INT16_MAX);

	/* test 0 */
	i16enc = 0;
	res = arsdk_cmd_enc(&cmd, &cmd_single_i16, i16enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_i16, &i16dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(i16dec, 0);

	/* test -1 */
	i16enc = -1;
	res = arsdk_cmd_enc(&cmd, &cmd_single_i16, i16enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_i16, &i16dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(i16dec, -1);
}

/** */
static void test_enc_dec_single_u16(void)
{
	int res = 0;
	struct arsdk_cmd cmd;

	const struct arsdk_arg_desc arg_single_u16[] = {
		s_arg_desc_u16,
	};
	uint16_t u16enc = 0;
	uint16_t u16dec = 0;

	struct arsdk_cmd_desc cmd_single_u16 = {
		"cmd_u16",
		UINT8_MAX,
		UINT8_MAX,
		UINT16_MAX,
		ARSDK_CMD_LIST_TYPE_NONE,
		ARSDK_CMD_BUFFER_TYPE_NON_ACK,
		ARSDK_CMD_TIMEOUT_POLICY_POP,
		arg_single_u16,
		sizeof(arg_single_u16) / sizeof(arg_single_u16[0]),
	};

	/* test 0 */
	res = arsdk_cmd_enc(&cmd, &cmd_single_u16, u16enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_u16, &u16dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(u16dec, 0);

	/* test max */
	u16enc = UINT16_MAX;
	res = arsdk_cmd_enc(&cmd, &cmd_single_u16, u16enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_u16, &u16dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(u16dec, UINT16_MAX);
}

/** */
static void test_enc_dec_single_i32(void)
{
	int res = 0;
	struct arsdk_cmd cmd;

	const struct arsdk_arg_desc arg_single_i32[] = {
		s_arg_desc_i32,
	};
	int32_t i32enc = INT32_MIN;
	int32_t i32dec = 0;

	struct arsdk_cmd_desc cmd_single_i32 = {
		"cmd_i32",
		UINT8_MAX,
		UINT8_MAX,
		UINT16_MAX,
		ARSDK_CMD_LIST_TYPE_NONE,
		ARSDK_CMD_BUFFER_TYPE_NON_ACK,
		ARSDK_CMD_TIMEOUT_POLICY_POP,
		arg_single_i32,
		sizeof(arg_single_i32) / sizeof(arg_single_i32[0]),
	};

	/* test min */
	res = arsdk_cmd_enc(&cmd, &cmd_single_i32, i32enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_i32, &i32dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(i32dec, INT32_MIN);

	/* test max */
	i32enc = INT32_MAX;
	res = arsdk_cmd_enc(&cmd, &cmd_single_i32, i32enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_i32, &i32dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(i32dec, INT32_MAX);

	/* test 0 */
	i32enc = 0;
	res = arsdk_cmd_enc(&cmd, &cmd_single_i32, i32enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_i32, &i32dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(i32dec, 0);

	/* test -1 */
	i32enc = -1;
	res = arsdk_cmd_enc(&cmd, &cmd_single_i32, i32enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_i32, &i32dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(i32dec, -1);
}

/** */
static void test_enc_dec_single_u32(void)
{
	int res = 0;
	struct arsdk_cmd cmd;

	const struct arsdk_arg_desc arg_single_u32[] = {
		s_arg_desc_u32,
	};
	uint32_t u32enc = 0;
	uint32_t u32dec = 0;

	struct arsdk_cmd_desc cmd_single_u32 = {
		"cmd_u32",
		UINT8_MAX,
		UINT8_MAX,
		UINT16_MAX,
		ARSDK_CMD_LIST_TYPE_NONE,
		ARSDK_CMD_BUFFER_TYPE_NON_ACK,
		ARSDK_CMD_TIMEOUT_POLICY_POP,
		arg_single_u32,
		sizeof(arg_single_u32) / sizeof(arg_single_u32[0]),
	};

	/* test 0 */
	res = arsdk_cmd_enc(&cmd, &cmd_single_u32, u32enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_u32, &u32dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(u32dec, 0);

	/* test max */
	u32enc = UINT32_MAX;
	res = arsdk_cmd_enc(&cmd, &cmd_single_u32, u32enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_u32, &u32dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(u32dec, UINT32_MAX);
}

/** */
static void test_enc_dec_single_i64(void)
{
	int res = 0;
	struct arsdk_cmd cmd;

	const struct arsdk_arg_desc arg_single_i64[] = {
		s_arg_desc_i64,
	};
	int64_t i64enc = INT64_MIN;
	int64_t i64dec = 0;

	struct arsdk_cmd_desc cmd_single_i64 = {
		"cmd_i64",
		UINT8_MAX,
		UINT8_MAX,
		UINT16_MAX,
		ARSDK_CMD_LIST_TYPE_NONE,
		ARSDK_CMD_BUFFER_TYPE_NON_ACK,
		ARSDK_CMD_TIMEOUT_POLICY_POP,
		arg_single_i64,
		sizeof(arg_single_i64) / sizeof(arg_single_i64[0]),
	};

	/* test min */
	res = arsdk_cmd_enc(&cmd, &cmd_single_i64, i64enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_i64, &i64dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(i64dec, INT64_MIN);

	/* test max */
	i64enc = INT64_MAX;
	res = arsdk_cmd_enc(&cmd, &cmd_single_i64, i64enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_i64, &i64dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(i64dec, INT64_MAX);

	/* test 0 */
	i64enc = 0;
	res = arsdk_cmd_enc(&cmd, &cmd_single_i64, i64enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_i64, &i64dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(i64dec, 0);

	/* test -1 */
	i64enc = -1;
	res = arsdk_cmd_enc(&cmd, &cmd_single_i64, i64enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_i64, &i64dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(i64dec, -1);
}

/** */
static void test_enc_dec_single_u64(void)
{
	int res = 0;
	struct arsdk_cmd cmd;

	const struct arsdk_arg_desc arg_single_u64[] = {
		s_arg_desc_u64,
	};
	uint64_t u64enc = 0;
	uint64_t u64dec = 0;

	struct arsdk_cmd_desc cmd_single_u64 = {
		"cmd_u64",
		UINT8_MAX,
		UINT8_MAX,
		UINT16_MAX,
		ARSDK_CMD_LIST_TYPE_NONE,
		ARSDK_CMD_BUFFER_TYPE_NON_ACK,
		ARSDK_CMD_TIMEOUT_POLICY_POP,
		arg_single_u64,
		sizeof(arg_single_u64) / sizeof(arg_single_u64[0]),
	};

	/* test 0 */
	res = arsdk_cmd_enc(&cmd, &cmd_single_u64, u64enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_u64, &u64dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(u64dec, 0);

	/* test max */
	u64enc = UINT64_MAX;
	res = arsdk_cmd_enc(&cmd, &cmd_single_u64, u64enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_u64, &u64dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(u64dec, UINT64_MAX);
}

/** */
static void test_enc_dec_single_float(void)
{
	int res = 0;
	struct arsdk_cmd cmd;

	const struct arsdk_arg_desc arg_single_float[] = {
		s_arg_desc_float,
	};
	float val_enc = FLT_MIN;
	float val_dec = 0;

	struct arsdk_cmd_desc cmd_single_float = {
		"cmd_float",
		UINT8_MAX,
		UINT8_MAX,
		UINT16_MAX,
		ARSDK_CMD_LIST_TYPE_NONE,
		ARSDK_CMD_BUFFER_TYPE_NON_ACK,
		ARSDK_CMD_TIMEOUT_POLICY_POP,
		arg_single_float,
		sizeof(arg_single_float) / sizeof(arg_single_float[0]),
	};

	res = arsdk_cmd_enc(&cmd, &cmd_single_float, val_enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_float, &val_dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(val_dec, val_enc);

	/* Test min */
	res = arsdk_cmd_enc(&cmd, &cmd_single_float, val_enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_float, &val_dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(val_dec, FLT_MIN);

	/* Test max */
	val_enc = FLT_MAX;
	res = arsdk_cmd_enc(&cmd, &cmd_single_float, val_enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_float, &val_dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(val_dec, FLT_MAX);

	/* Test 0 */
	val_enc = 0;
	res = arsdk_cmd_enc(&cmd, &cmd_single_float, val_enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_float, &val_dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(val_dec, 0);

	/* Test -1 */
	val_enc = -1;
	res = arsdk_cmd_enc(&cmd, &cmd_single_float, val_enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_float, &val_dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(val_dec, -1);

	/* Test FLT_EPSILON */
	val_enc = FLT_EPSILON;
	res = arsdk_cmd_enc(&cmd, &cmd_single_float, val_enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_float, &val_dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(val_dec, FLT_EPSILON);
}

/** */
static void test_enc_dec_single_double(void)
{
	int res = 0;
	struct arsdk_cmd cmd;

	const struct arsdk_arg_desc arg_single_double[] = {
		s_arg_desc_double,
	};
	double double_enc = DBL_MIN;
	double double_dec = 0;

	struct arsdk_cmd_desc cmd_single_double = {
		"cmd_bouble",
		UINT8_MAX,
		UINT8_MAX,
		UINT16_MAX,
		ARSDK_CMD_LIST_TYPE_NONE,
		ARSDK_CMD_BUFFER_TYPE_NON_ACK,
		ARSDK_CMD_TIMEOUT_POLICY_POP,
		arg_single_double,
		sizeof(arg_single_double) / sizeof(arg_single_double[0]),
	};

	/* Test min */
	res = arsdk_cmd_enc(&cmd, &cmd_single_double, double_enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_double, &double_dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(double_dec, DBL_MIN);

	/* Test max */
	double_enc = DBL_MAX;
	res = arsdk_cmd_enc(&cmd, &cmd_single_double, double_enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_double, &double_dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(double_dec, DBL_MAX);

	/* Test 0 */
	double_enc = 0;
	res = arsdk_cmd_enc(&cmd, &cmd_single_double, double_enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_double, &double_dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(double_dec, 0);

	/* Test -1 */
	double_enc = -1;
	res = arsdk_cmd_enc(&cmd, &cmd_single_double, double_enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_double, &double_dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(double_dec, -1);

	/* Test DBL_EPSILON */
	double_enc = DBL_EPSILON;
	res = arsdk_cmd_enc(&cmd, &cmd_single_double, double_enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_double, &double_dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(double_dec, DBL_EPSILON);
}

/** */
static void test_enc_dec_single_enum_max(void)
{
	int res = 0;
	struct arsdk_cmd cmd;

	const struct arsdk_arg_desc arg_single_enum[] = {
			s_arg_desc_enum,
	};
	int enum_enc = TEST_ENUM_TYPE_B;
	int enum_dec = 0;

	struct arsdk_cmd_desc cmd_single_enum = {
		"cmd_enum",
		UINT8_MAX,
		UINT8_MAX,
		UINT16_MAX,
		ARSDK_CMD_LIST_TYPE_NONE,
		ARSDK_CMD_BUFFER_TYPE_NON_ACK,
		ARSDK_CMD_TIMEOUT_POLICY_POP,
		arg_single_enum,
		sizeof(arg_single_enum) / sizeof(arg_single_enum[0]),
	};

	res = arsdk_cmd_enc(&cmd, &cmd_single_enum, enum_enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_enum, &enum_dec);
	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(enum_dec, enum_enc);
}

#define CMD_DEFAULT_SIZE_MAX 256
#define STR_TOO_LARGE_SIZE (CMD_DEFAULT_SIZE_MAX * 2)
/** */
static void test_enc_dec_single_string_force_realloc(void)
{
	int res = 0;
	struct arsdk_cmd cmd;

	const struct arsdk_arg_desc arg_single_string[] = {
		s_arg_desc_string,
	};
	char str_enc [STR_TOO_LARGE_SIZE];
	char *str_dec = NULL;

	memset(str_enc, 'A', STR_TOO_LARGE_SIZE-1);
	str_enc[STR_TOO_LARGE_SIZE-1] = '\0';

	struct arsdk_cmd_desc cmd_single_string = {
		"cmd_str",
		UINT8_MAX,
		UINT8_MAX,
		UINT16_MAX,
		ARSDK_CMD_LIST_TYPE_NONE,
		ARSDK_CMD_BUFFER_TYPE_NON_ACK,
		ARSDK_CMD_TIMEOUT_POLICY_POP,
		arg_single_string,
		sizeof(arg_single_string) / sizeof(arg_single_string[0]),
	};

	res = arsdk_cmd_enc(&cmd, &cmd_single_string, str_enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_single_string, &str_dec);

	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_STRING_EQUAL(str_dec, str_enc);
}

/** */
static void test_enc_dec_all_types(void)
{
	int res = 0;
	struct arsdk_cmd cmd;

	const struct arsdk_arg_desc arg_all_types[] = {
		s_arg_desc_i8,
		s_arg_desc_u8,
		s_arg_desc_i16,
		s_arg_desc_u16,
		s_arg_desc_i32,
		s_arg_desc_u32,
		s_arg_desc_i64,
		s_arg_desc_u64,
		s_arg_desc_float,
		s_arg_desc_double,
		s_arg_desc_string,
		s_arg_desc_enum,
	};

	int8_t i8_enc = INT8_MIN;
	uint8_t u8_enc = UINT8_MAX;
	int16_t i16_enc = INT16_MIN;
	uint16_t u16_enc = UINT16_MAX;
	int32_t i32_enc = INT32_MIN;
	uint32_t u32_enc = UINT32_MAX;
	int64_t i64_enc = INT64_MIN;
	uint64_t u64_enc = UINT64_MAX;
	float flt_enc = FLT_EPSILON;
	double dbl_enc = DBL_EPSILON;
	char str_enc[] =  "ABC";
	int enum_enc = TEST_ENUM_TYPE_B;

	int8_t i8_dec = 0;
	uint8_t u8_dec = 0;
	int16_t i16_dec = 0;
	uint16_t u16_dec = 0;
	int32_t i32_dec = 0;
	uint32_t u32_dec = 0;
	int64_t i64_dec = 0;
	uint64_t u64_dec = 0;
	float flt_dec = 0.0f;
	double dbl_dec = 0.0;
	char *str_dec =  NULL;
	int enum_dec = 0;

	struct arsdk_cmd_desc cmd_all_types = {
		"all",
		UINT8_MAX,
		UINT8_MAX,
		UINT16_MAX,
		ARSDK_CMD_LIST_TYPE_NONE,
		ARSDK_CMD_BUFFER_TYPE_NON_ACK,
		ARSDK_CMD_TIMEOUT_POLICY_POP,
		arg_all_types,
		sizeof(arg_all_types) / sizeof(arg_all_types[0]),
	};

	res = arsdk_cmd_enc(&cmd, &cmd_all_types, i8_enc, u8_enc, i16_enc,
			u16_enc, i32_enc, u32_enc, i64_enc, u64_enc,
			flt_enc, dbl_enc, str_enc, enum_enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_all_types, &i8_dec, &u8_dec, &i16_dec,
			&u16_dec, &i32_dec, &u32_dec, &i64_dec, &u64_dec,
			&flt_dec, &dbl_dec, &str_dec, &enum_dec);

	CU_ASSERT_EQUAL(res, 0);
	CU_ASSERT_EQUAL(i8_dec, i8_enc);
	CU_ASSERT_EQUAL(u8_dec, u8_enc);
	CU_ASSERT_EQUAL(i16_dec, i16_enc);
	CU_ASSERT_EQUAL(u16_dec, u16_enc);
	CU_ASSERT_EQUAL(i32_dec, i32_enc);
	CU_ASSERT_EQUAL(u32_dec, u32_enc);
	CU_ASSERT_EQUAL(i64_dec, i64_enc);
	CU_ASSERT_EQUAL(u64_dec, u64_enc);
	CU_ASSERT_EQUAL(flt_dec, flt_enc);
	CU_ASSERT_EQUAL(dbl_dec, dbl_enc);
	CU_ASSERT_STRING_EQUAL(str_dec, str_enc);
	CU_ASSERT_EQUAL(enum_dec, enum_enc);
}

/** */
static void test_enc_dec_bad_cmd(void)
{
	int res = 0;
	struct arsdk_cmd cmd;

	const struct arsdk_arg_desc arg_a[] = {
		s_arg_desc_i8,
		s_arg_desc_u32,
	};

	const struct arsdk_arg_desc arg_b[] = {
		s_arg_desc_u32,
		s_arg_desc_i8,
	};

	int8_t i8_enc = INT8_MIN;
	uint32_t u32_enc = UINT32_MAX;

	int8_t i8_dec = 0;
	uint32_t u32_dec = 0;

	struct arsdk_cmd_desc cmd_a = {
		"cmd_a",
		UINT8_MAX,
		UINT8_MAX,
		UINT16_MAX,
		ARSDK_CMD_LIST_TYPE_NONE,
		ARSDK_CMD_BUFFER_TYPE_NON_ACK,
		ARSDK_CMD_TIMEOUT_POLICY_POP,
		arg_a,
		sizeof(arg_a) / sizeof(arg_a[0]),
	};

	struct arsdk_cmd_desc cmd_b = {
		"cmd_b",
		UINT8_MAX,
		UINT8_MAX,
		0,
		ARSDK_CMD_LIST_TYPE_NONE,
		ARSDK_CMD_BUFFER_TYPE_NON_ACK,
		ARSDK_CMD_TIMEOUT_POLICY_POP,
		arg_b,
		sizeof(arg_b) / sizeof(arg_b[0]),
	};

	res = arsdk_cmd_enc(&cmd, &cmd_a, i8_enc, u32_enc);
	CU_ASSERT_EQUAL(res, 0);

	res = arsdk_cmd_dec(&cmd, &cmd_b, &u32_dec, &i8_dec);
	CU_ASSERT_EQUAL(res, -EINVAL);
}

/** */
static void test_enc_dec_bad_arg(void)
{
	int res = 0;
	struct arsdk_cmd cmd;

	const struct arsdk_arg_desc arg_a[] = {
		s_arg_desc_i8,
		s_arg_desc_u32,
	};

	int8_t i8_enc = INT8_MIN;
	uint32_t u32_enc = UINT32_MAX;

	int8_t i8_dec = 0;
	uint32_t u32_dec = 0;

	struct arsdk_cmd_desc cmd_a = {
		"cmd_a",
		UINT8_MAX,
		UINT8_MAX,
		UINT16_MAX,
		ARSDK_CMD_LIST_TYPE_NONE,
		ARSDK_CMD_BUFFER_TYPE_NON_ACK,
		ARSDK_CMD_TIMEOUT_POLICY_POP,
		arg_a,
		sizeof(arg_a) / sizeof(arg_a[0]),
	};

	res = arsdk_cmd_enc(NULL, &cmd_a, u32_enc, i8_enc);
	CU_ASSERT_EQUAL(res, -EINVAL);
	res = arsdk_cmd_enc(&cmd, NULL, u32_enc, i8_enc);
	CU_ASSERT_EQUAL(res, -EINVAL);

	res = arsdk_cmd_dec(NULL, &cmd_a, &u32_dec, &i8_dec);
	CU_ASSERT_EQUAL(res, -EINVAL);
	res = arsdk_cmd_dec(&cmd, NULL, &u32_dec, &i8_dec);
	CU_ASSERT_EQUAL(res, -EINVAL);
}

/** */
static void test_enc_dec_bad_buf(void)
{
	int res = 0;
	struct arsdk_cmd cmd;

	const struct arsdk_arg_desc arg_a[] = {
		s_arg_desc_i8,
		s_arg_desc_u32,
	};

	int8_t i8_enc = INT8_MIN;
	uint32_t u32_enc = UINT32_MAX;

	int8_t i8_dec = 0;
	uint32_t u32_dec = 0;

	struct arsdk_cmd_desc cmd_a = {
		"cmd_a",
		UINT8_MAX,
		UINT8_MAX,
		UINT16_MAX,
		ARSDK_CMD_LIST_TYPE_NONE,
		ARSDK_CMD_BUFFER_TYPE_NON_ACK,
		ARSDK_CMD_TIMEOUT_POLICY_POP,
		arg_a,
		sizeof(arg_a) / sizeof(arg_a[0]),
	};

	res = arsdk_cmd_enc(&cmd, &cmd_a, i8_enc, u32_enc);
	CU_ASSERT_EQUAL(res, 0);

	/* test buffer null */
	pomp_buffer_unref(cmd.buf);
	cmd.buf = NULL;
	res = arsdk_cmd_dec(&cmd, &cmd_a, &i8_dec, &u32_dec);
	CU_ASSERT_EQUAL(res, -EINVAL);

	/* test bad size buffer */
	cmd.buf = pomp_buffer_new(2);
	res = arsdk_cmd_dec(&cmd, &cmd_a, &i8_dec, &u32_dec);
	CU_ASSERT_EQUAL(res, -EINVAL);
}

/** */
static void test_enc_dec(void)
{
	test_enc_dec_void();
	test_enc_dec_single_i8();
	test_enc_dec_single_u8();
	test_enc_dec_single_i16();
	test_enc_dec_single_u16();
	test_enc_dec_single_i32();
	test_enc_dec_single_u32();
	test_enc_dec_single_i64();
	test_enc_dec_single_u64();
	test_enc_dec_single_float();
	test_enc_dec_single_double();
	test_enc_dec_single_enum_max();
	test_enc_dec_single_string_force_realloc();

	test_enc_dec_all_types();

	test_enc_dec_bad_cmd();
	test_enc_dec_bad_arg();
	test_enc_dec_bad_buf();
}

/* Disable some gcc warnings for test suite descriptions */
#ifdef __GNUC__
#  pragma GCC diagnostic ignored "-Wcast-qual"
#endif /* __GNUC__ */

/** */
static CU_TestInfo s_enc_dec_tests[] = {
	{(char *)"enc_dec", &test_enc_dec},
	CU_TEST_INFO_NULL,
};

/** */
/*extern*/ CU_SuiteInfo g_suites_enc_dec[] = {
	{(char *)"enc_dec", NULL, NULL, s_enc_dec_tests},
	CU_SUITE_INFO_NULL,
};
