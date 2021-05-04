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

/** Allocation step in buffer (shall be a power of 2) */
#define BUFFER_ALLOC_STEP	(256u)

/** Align size up to next allocation step */
#define BUFFER_ALIGN_ALLOC_SIZE(_x) \
	(((_x) + BUFFER_ALLOC_STEP - 1) & (~(BUFFER_ALLOC_STEP - 1)))

/** */
struct encoder {
	struct pomp_buffer  *buf;
	void                *data;
	size_t              len;
	size_t              capacity;
	size_t              off;
};

/**
 */
static int encoder_init(struct encoder *enc)
{
	/* Allocate buffer */
	enc->buf = pomp_buffer_new(BUFFER_ALLOC_STEP);
	if (enc->buf == NULL)
		return -ENOMEM;

	/* Get data from buffer */
	int res = pomp_buffer_get_data(enc->buf, &enc->data, &enc->len,
			&enc->capacity);
	if (res < 0)
		return res;

	enc->off = 0;
	return 0;
}

/**
 */
static void encoder_clear(struct encoder *enc)
{
	if (enc->buf != NULL)
		pomp_buffer_unref(enc->buf);
	memset(enc, 0, sizeof(*enc));
}

/**
 */
static int encoder_ensure_capacity(struct encoder *enc, size_t capacity)
{
	int res = 0;

	/* Check if there is something to do */
	if (capacity <= enc->capacity)
		return 0;

	/* Resize buffer after aligning requested capacity */
	capacity = BUFFER_ALIGN_ALLOC_SIZE(capacity);
	res = pomp_buffer_set_capacity(enc->buf, capacity);
	if (res < 0)
		return res;

	/* Get data from buffer */
	return pomp_buffer_get_data(enc->buf, &enc->data, &enc->len,
			&enc->capacity);
}

/**
 */
static int encoder_write(struct encoder *enc, const void *p, size_t n)
{
	int res = 0;

	/* Make sure there is enough room in data buffer */
	res = encoder_ensure_capacity(enc, enc->off + n);
	if (res < 0)
		return res;

	/* Copy data */
	memcpy((uint8_t *)enc->data + enc->off, p, n);
	enc->off += n;
	return 0;
}

/**
 */
static int encoder_write_i8(struct encoder *enc, int8_t v)
{
	uint8_t d = (uint8_t)v;
	return encoder_write(enc, &d, sizeof(d));
}

/**
 */
static int encoder_write_u8(struct encoder *enc, uint8_t v)
{
	uint8_t d = v;
	return encoder_write(enc, &d, sizeof(d));
}

/**
 */
static int encoder_write_i16(struct encoder *enc, int16_t v)
{
	uint16_t d = ARSDK_HTOLE16(v);
	return encoder_write(enc, &d, sizeof(d));
}

/**
 */
static int encoder_write_u16(struct encoder *enc, uint16_t v)
{
	uint16_t d = ARSDK_HTOLE16(v);
	return encoder_write(enc, &d, sizeof(d));
}

/**
 */
static int encoder_write_i32(struct encoder *enc, int32_t v)
{
	uint32_t d = ARSDK_HTOLE32(v);
	return encoder_write(enc, &d, sizeof(d));
}

/**
 */
static int encoder_write_u32(struct encoder *enc, uint32_t v)
{
	uint32_t d = ARSDK_HTOLE32(v);
	return encoder_write(enc, &d, sizeof(d));
}

/**
 */
static int encoder_write_i64(struct encoder *enc, int64_t v)
{
	uint64_t d = ARSDK_HTOLE64(v);
	return encoder_write(enc, &d, sizeof(d));
}

/**
 */
static int encoder_write_u64(struct encoder *enc, uint64_t v)
{
	uint64_t d = ARSDK_HTOLE64(v);
	return encoder_write(enc, &d, sizeof(d));
}

/**
 */
static int encoder_write_f32(struct encoder *enc, float v)
{
	union {
		float f32;
		uint32_t u32;
	} d;
	d.f32 = v;
	d.u32 = ARSDK_HTOLE32(d.u32);
	return encoder_write(enc, &d, sizeof(d));
}

/**
 */
static int encoder_write_str(struct encoder *enc, const char *v)
{
	/* Compute string length, add null byte */
	size_t len = strlen(v) + 1;
	return encoder_write(enc, v, len);
}

/**
 */
static int encoder_write_f64(struct encoder *enc, double v)
{
	union {
		double f64;
		uint64_t u64;
	} d;
	d.f64 = v;
	d.u64 = ARSDK_HTOLE64(d.u64);
	return encoder_write(enc, &d, sizeof(d));
}

/**
 */
static int encoder_write_binary(struct encoder *enc,
		const struct arsdk_binary *binary)
{
	int res = 0;

	res = encoder_write_u32(enc, binary->len);
	if (res < 0)
		return res;

	res = encoder_write(enc, binary->cdata, binary->len);
	if (res < 0)
		return res;

	return res;
}

/**
 */
static int cmd_encv_internal(struct arsdk_cmd *cmd,
			     const struct arsdk_cmd_desc *desc, size_t argc,
			     const struct arsdk_value *argv, va_list args)
{
	int res = 0;
	struct encoder enc;
	uint32_t i = 0;
	const struct arsdk_arg_desc *arg_desc = NULL;
	struct arsdk_value val;
	memset(&val, 0, sizeof(val));

	ARSDK_RETURN_ERR_IF_FAILED(cmd != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(desc != NULL, -EINVAL);

	if (argv != NULL)
		ARSDK_RETURN_ERR_IF_FAILED(argc == desc->arg_desc_count,
					   -EINVAL);

	arsdk_cmd_init(cmd);

	/* Initialize encoder */
	res = encoder_init(&enc);
	if (res < 0)
		return res;

	/* Write project/class/cmd ids */
	res = encoder_write_u8(&enc, desc->prj_id);
	if (res < 0)
		goto out;
	res = encoder_write_u8(&enc, desc->cls_id);
	if (res < 0)
		goto out;
	res = encoder_write_u16(&enc, desc->cmd_id);
	if (res < 0)
		goto out;
	cmd->prj_id = desc->prj_id;
	cmd->cls_id = desc->cls_id;
	cmd->cmd_id = desc->cmd_id;
	cmd->id = ARSDK_CMD_FULL_ID(cmd->prj_id, cmd->cls_id, cmd->cmd_id);

	/* Arguments */
	for (i = 0; i < desc->arg_desc_count; i++) {
		arg_desc = &desc->arg_desc_table[i];

		/* Arguments given by argv */
		if (argv != NULL) {
			val = argv[i];

			/* compare desc and given type */
			if (arg_desc->type != argv[i].type) {
				res = -EINVAL;
				goto out;
			}
		}

		switch (arg_desc->type) {
		case ARSDK_ARG_TYPE_I8:
			if (argv == NULL) {
				val.type = ARSDK_ARG_TYPE_I8;
				val.data.i8 = (int8_t)va_arg(args, int);
			}
			res = encoder_write_i8(&enc, val.data.i8);
			if (res < 0)
				goto out;
			break;

		case ARSDK_ARG_TYPE_U8:
			if (argv == NULL) {
				val.type = ARSDK_ARG_TYPE_U8;
				val.data.u8 = (uint8_t)va_arg(args,
							      unsigned int);
			}
			res = encoder_write_u8(&enc, val.data.u8);
			if (res < 0)
				goto out;
			break;

		case ARSDK_ARG_TYPE_I16:
			if (argv == NULL) {
				val.type = ARSDK_ARG_TYPE_I16;
				val.data.i16 = (int16_t)va_arg(args, int);
			}
			res = encoder_write_i16(&enc, val.data.i16);
			if (res < 0)
				goto out;
			break;

		case ARSDK_ARG_TYPE_U16:
			if (argv == NULL) {
				val.type = ARSDK_ARG_TYPE_U16;
				val.data.u16 = (uint16_t)va_arg(args,
								unsigned int);
			}
			res = encoder_write_u16(&enc, val.data.u16);
			if (res < 0)
				goto out;
			break;

		case ARSDK_ARG_TYPE_I32:
			if (argv == NULL) {
				val.type = ARSDK_ARG_TYPE_I32;
				val.data.i32 = va_arg(args, int);
			}
			res = encoder_write_i32(&enc, val.data.i32);
			if (res < 0)
				goto out;
			break;

		case ARSDK_ARG_TYPE_U32:
			if (argv == NULL) {
				val.type = ARSDK_ARG_TYPE_U32;
				val.data.u32 = va_arg(args, unsigned int);
			}
			res = encoder_write_u32(&enc, val.data.u32);
			if (res < 0)
				goto out;
			break;

		case ARSDK_ARG_TYPE_I64:
			if (argv == NULL) {
				val.type = ARSDK_ARG_TYPE_I64;
				val.data.i64 = va_arg(args, int64_t);
			}
			res = encoder_write_i64(&enc, val.data.i64);
			if (res < 0)
				goto out;
			break;

		case ARSDK_ARG_TYPE_U64:
			if (argv == NULL) {
				val.type = ARSDK_ARG_TYPE_U64;
				val.data.u64 = va_arg(args, uint64_t);
			}
			res = encoder_write_u64(&enc, val.data.u64);
			if (res < 0)
				goto out;
			break;

		case ARSDK_ARG_TYPE_FLOAT:
			if (argv == NULL) {
				val.type = ARSDK_ARG_TYPE_FLOAT;
				/* float shall be extracted as double */
				val.data.f32 = (float)va_arg(args, double);
			}
			res = encoder_write_f32(&enc, val.data.f32);
			if (res < 0)
				goto out;
			break;

		case ARSDK_ARG_TYPE_DOUBLE:
			if (argv == NULL) {
				val.type = ARSDK_ARG_TYPE_DOUBLE;
				val.data.f64 = va_arg(args, double);
			}
			res = encoder_write_f64(&enc, val.data.f64);
			if (res < 0)
				goto out;
			break;

		case ARSDK_ARG_TYPE_STRING:
			if (argv == NULL) {
				val.type = ARSDK_ARG_TYPE_STRING;
				val.data.cstr = va_arg(args, const char *);
			}
			if (val.data.cstr == NULL)
				val.data.cstr = "";
			res = encoder_write_str(&enc, val.data.cstr);
			if (res < 0)
				goto out;
			break;

		case ARSDK_ARG_TYPE_ENUM:
			if (argv == NULL) {
				val.type = ARSDK_ARG_TYPE_ENUM;
				/* enum shall be extracted as i32 */
				val.data.i32 = va_arg(args, int);
			}
			res = encoder_write_i32(&enc, val.data.i32);
			if (res < 0)
				goto out;
			break;

		case ARSDK_ARG_TYPE_BINARY:
			if (argv == NULL) {
				val.type = ARSDK_ARG_TYPE_BINARY;
				val.data.binary = *va_arg(args,
						const struct arsdk_binary *);
			}

			res = encoder_write_binary(&enc, &val.data.binary);
			if (res < 0)
				goto out;
			break;

		default:
			res = -EINVAL;
			ARSDK_LOGW("encoder: unknown argument type %d",
					arg_desc->type);
			goto out;
		}
	}

	/* Set final length of buffer */
	res = pomp_buffer_set_len(enc.buf, enc.off);

out:
	/* In case of success, save a new ref for caller */
	if (res == 0) {
		cmd->buf = enc.buf;
		pomp_buffer_ref(cmd->buf);
	}

	/* Release our internal ref */
	encoder_clear(&enc);
	return res;
}

/* function needed because arsdk_cmd_enc_argv cannot pass NULL as va_list. */
static int cmd_enc_internal(struct arsdk_cmd *cmd,
			    const struct arsdk_cmd_desc *desc, size_t argc,
			    const struct arsdk_value *argv, ...)
{
	int res = 0;
	va_list args;
	va_start(args, argv);
	res = cmd_encv_internal(cmd, desc, argc, argv, args);
	va_end(args);
	return res;
}

int arsdk_cmd_enc_argv(struct arsdk_cmd *cmd,
		       const struct arsdk_cmd_desc *desc, size_t argc,
		       const struct arsdk_value *argv)
{
	return cmd_enc_internal(cmd, desc, argc, argv);
}

int arsdk_cmd_enc(struct arsdk_cmd *cmd, const struct arsdk_cmd_desc *desc, ...)
{
	int res = 0;
	va_list args;
	va_start(args, desc);
	res = cmd_encv_internal(cmd, desc, 0, NULL, args);
	va_end(args);
	return res;
}
