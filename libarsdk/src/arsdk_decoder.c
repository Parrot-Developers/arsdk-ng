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

/** */
struct decoder {
	struct pomp_buffer  *buf;
	const void          *cdata;
	size_t              len;
	size_t              capacity;
	size_t              off;
};

/**
 */
static void decoder_init(struct decoder *dec, struct pomp_buffer *buf)
{
	/* Get data from buffer */
	pomp_buffer_get_cdata(buf, &dec->cdata, &dec->len, &dec->capacity);
	dec->buf = buf;
	dec->off = 0;
}

/**
 */
static void decoder_clear(struct decoder *dec)
{
	/* Simply reset the structure, buffer was not ours */
	memset(dec, 0, sizeof(*dec));
}

/**
 */
static int decoder_read(struct decoder *dec, void *p, size_t n)
{
	/* Make sure there is enough room in data buffer */
	if (dec->off + n > dec->len)
		return -EINVAL;

	/* Copy data */
	memcpy(p, (const uint8_t *)dec->cdata + dec->off, n);
	dec->off += n;
	return 0;
}

/**
 */
static int decoder_cread(struct decoder *dec, const void **p, size_t n)
{
	/* Make sure there is enough room in data buffer */
	if (dec->off + n > dec->len)
		return -EINVAL;

	/* Simply set start of data */
	*p = (const uint8_t *)dec->cdata + dec->off;
	dec->off += n;
	return 0;
}

/**
 */
static int decoder_read_i8(struct decoder *dec, int8_t *v)
{
	int res = 0;
	uint8_t d = 0;
	res = decoder_read(dec, &d, sizeof(d));
	*v = (int8_t)d;
	return res;
}

/**
 */
static int decoder_read_u8(struct decoder *dec, uint8_t *v)
{
	int res = 0;
	uint8_t d = 0;
	res = decoder_read(dec, &d, sizeof(d));
	*v = d;
	return res;
}

/**
 */
static int decoder_read_i16(struct decoder *dec, int16_t *v)
{
	int res = 0;
	uint16_t d = 0;
	res = decoder_read(dec, &d, sizeof(d));
	*v = (int16_t)ARSDK_LE16TOH(d);
	return res;
}

/**
 */
static int decoder_read_u16(struct decoder *dec, uint16_t *v)
{
	int res = 0;
	uint16_t d = 0;
	res = decoder_read(dec, &d, sizeof(d));
	*v = ARSDK_LE16TOH(d);
	return res;
}

/**
 */
static int decoder_read_i32(struct decoder *dec, int32_t *v)
{
	int res = 0;
	uint32_t d = 0;
	res = decoder_read(dec, &d, sizeof(d));
	*v = (int32_t)ARSDK_LE32TOH(d);
	return res;
}

/**
 */
static int decoder_read_u32(struct decoder *dec, uint32_t *v)
{
	int res = 0;
	uint32_t d = 0;
	res = decoder_read(dec, &d, sizeof(d));
	*v = ARSDK_LE32TOH(d);
	return res;
}

/**
 */
static int decoder_read_i64(struct decoder *dec, int64_t *v)
{
	int res = 0;
	uint64_t d = 0;
	res = decoder_read(dec, &d, sizeof(d));
	*v = (int64_t)ARSDK_LE64TOH(d);
	return res;
}

/**
 */
static int decoder_read_u64(struct decoder *dec, uint64_t *v)
{
	int res = 0;
	uint64_t d = 0;
	res = decoder_read(dec, &d, sizeof(d));
	*v = ARSDK_LE64TOH(d);
	return res;
}

/**
 */
static int decoder_read_f32(struct decoder *dec, float *v)
{
	int res = 0;
	union {
		float f32;
		uint32_t u32;
	} d = {0};
	res = decoder_read(dec, &d, sizeof(d));
	d.u32 = ARSDK_LE32TOH(d.u32);
	*v = d.f32;
	return res;
}

/**
 */
static int decoder_read_f64(struct decoder *dec, double *v)
{
	int res = 0;
	union {
		double f64;
		uint64_t u64;
	} d = {0};
	res = decoder_read(dec, &d, sizeof(d));
	d.u64 = ARSDK_LE64TOH(d.u64);
	*v = d.f64;
	return res;
}

/**
 */
static int decoder_read_cstr(struct decoder *dec, const char **v)
{
	size_t pos = dec->off;

	/* Search for null byte in data */
	while (pos < dec->len) {
		if (*((const uint8_t *)dec->cdata + pos) == '\0') {
			return decoder_cread(dec, (const void **)v,
					pos - dec->off + 1);
		}
		pos++;
	}

	ARSDK_LOGW("decoder: string not null terminated");
	return -EINVAL;
}

/**
 */
static int decoder_read_multiset(struct decoder *dec,
		struct arsdk_multiset *multi)
{
	int res = 0;
	uint16_t multiset_size = 0;
	size_t multiset_end = 0;
	uint16_t single_cmd_size = 0;
	struct pomp_buffer *buf = NULL;
	struct arsdk_cmd *cmd = NULL;
	const struct arsdk_cmd_desc *decs = NULL;
	size_t desc_i = 0;

	ARSDK_RETURN_ERR_IF_FAILED(multi != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(multi->cmds != NULL, -EINVAL);

	res = decoder_read_u16(dec, &multiset_size);
	if (res < 0)
		return res;

	multiset_end = dec->off + multiset_size;
	if (dec->len < multiset_end)
		return -EINVAL;

	while (dec->off < multiset_end) {
		cmd = &multi->cmds[multi->n_cmds];

		res = decoder_read_u16(dec, &single_cmd_size);
		if (res < 0)
			return res;

		buf = pomp_buffer_new_with_data(
				((const uint8_t *)dec->cdata + dec->off),
				single_cmd_size);
		if (buf == NULL)
			return -ENOMEM;

		arsdk_cmd_init_with_buf(cmd, buf);
		pomp_buffer_unref(buf);

		/* Try to decode header of command, Notify reception */
		res = arsdk_cmd_dec_header(cmd);
		if (res < 0) {
			ARSDK_LOG_ERRNO("arsdk_cmd_dec_header", -res);
		} else {
			/* check multi setting and command matching */
			for (desc_i = 0; desc_i < multi->n_descs; desc_i++) {
				decs = multi->descs[desc_i];
				if ((decs->prj_id == cmd->prj_id) &&
				    (decs->cls_id == cmd->cls_id) &&
				    (decs->cmd_id == cmd->cmd_id)) {
					multi->n_cmds++;
					break;
				}
			}
		}

		dec->off += single_cmd_size;
	}

	return res;
}

/**
 */
#if defined(__GNUC__) && defined(__MINGW32__) && !defined(__clang__)
__attribute__((__format__(__gnu_printf__, 4, 5)))
#elif defined(__GNUC__)
__attribute__((__format__(__printf__, 4, 5)))
#endif
static void fmt_append(char *buf, size_t len, size_t *off, const char *fmt, ...)
{
	int res = 0;
	va_list args;
	va_start(args, fmt);
	res = vsnprintf(buf + *off, len - *off, fmt, args);
	if (res >= 0 && (size_t)res < len - *off)
		(*off) += (size_t)res;
	va_end(args);
}

static const char *get_enum_str(const struct arsdk_arg_desc *arg_desc,
		int32_t val)
{
	uint32_t i = 0;
	for (i = 0; i < arg_desc->enum_desc_count; i++) {
		if (arg_desc->enum_desc_table[i].value == val)
			return arg_desc->enum_desc_table[i].name;
	}
	return NULL;
}

static void fmt_append_bitfield(char *buf, size_t len, size_t *off,
				const struct arsdk_arg_desc *arg_desc,
				uint64_t val)
{
	const char *enum_str;
	int first;
	size_t i;

	first = 1;
	for (i = 0; i < arg_desc->enum_desc_count && i < 64; i++) {
		if (!(val & (1ULL << i)))
			continue;
		if (!first)
			fmt_append(buf, len, off, "|");
		first = 0;
		enum_str = get_enum_str(arg_desc, i);
		if (enum_str)
			fmt_append(buf, len, off, "%s", enum_str);
		else
			fmt_append(buf, len, off, "UNKNOWN(%zd)", i);
	}

	if (first)
		fmt_append(buf, len, off, "0");
}

/**
 */
int arsdk_cmd_dec(const struct arsdk_cmd *cmd,
		const struct arsdk_cmd_desc *desc, ...)
{
	int res = 0;
	struct decoder dec;
	va_list args;
	uint32_t i = 0;
	uint8_t project_id = 0, class_id = 0;
	uint16_t cmd_id = 0;
	const struct arsdk_arg_desc *arg_desc = NULL;
	struct arsdk_value val;

	ARSDK_RETURN_ERR_IF_FAILED(cmd != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cmd->buf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(desc != NULL, -EINVAL);

	/* Initialize decoder */
	decoder_init(&dec, cmd->buf);

	va_start(args, desc);

	/* Read project/class/cmd ids */
	res = decoder_read_u8(&dec, &project_id);
	if (res < 0)
		goto out;

	res = decoder_read_u8(&dec, &class_id);
	if (res < 0)
		goto out;

	res = decoder_read_u16(&dec, &cmd_id);
	if (res < 0)
		goto out;

	/* Check validity */
	if (project_id != desc->prj_id) {
		res = -EINVAL;
		ARSDK_LOGW("decoder: project id mismatch: %d(%d)",
				project_id, desc->prj_id);
		goto out;
	}
	if (class_id != desc->cls_id) {
		res = -EINVAL;
		ARSDK_LOGW("decoder: class id mismatch: %d(%d)",
				class_id, desc->cls_id);
		goto out;
	}
	if (cmd_id != desc->cmd_id) {
		res = -EINVAL;
		ARSDK_LOGW("decoder: command id mismatch: %d(%d)",
				cmd_id, desc->cmd_id);
		goto out;
	}

	/* Arguments */
	for (i = 0; i < desc->arg_desc_count; i++) {
		arg_desc = &desc->arg_desc_table[i];
		switch (arg_desc->type) {
		case ARSDK_ARG_TYPE_I8:
			val.type = ARSDK_ARG_TYPE_I8;
			res = decoder_read_i8(&dec, &val.data.i8);
			if (res < 0)
				goto out;
			*va_arg(args, int8_t *) = val.data.i8;
			break;

		case ARSDK_ARG_TYPE_U8:
			val.type = ARSDK_ARG_TYPE_U8;
			res = decoder_read_u8(&dec, &val.data.u8);
			if (res < 0)
				goto out;
			*va_arg(args, uint8_t *) = val.data.u8;
			break;

		case ARSDK_ARG_TYPE_I16:
			val.type = ARSDK_ARG_TYPE_I16;
			res = decoder_read_i16(&dec, &val.data.i16);
			if (res < 0)
				goto out;
			*va_arg(args, int16_t *) = val.data.i16;
			break;

		case ARSDK_ARG_TYPE_U16:
			val.type = ARSDK_ARG_TYPE_U16;
			res = decoder_read_u16(&dec, &val.data.u16);
			if (res < 0)
				goto out;
			*va_arg(args, uint16_t *) = val.data.u16;
			break;

		case ARSDK_ARG_TYPE_I32:
			val.type = ARSDK_ARG_TYPE_I32;
			res = decoder_read_i32(&dec, &val.data.i32);
			if (res < 0)
				goto out;
			*va_arg(args, int32_t *) = val.data.i32;
			break;

		case ARSDK_ARG_TYPE_U32:
			val.type = ARSDK_ARG_TYPE_U32;
			res = decoder_read_u32(&dec, &val.data.u32);
			if (res < 0)
				goto out;
			*va_arg(args, uint32_t *) = val.data.u32;
			break;

		case ARSDK_ARG_TYPE_I64:
			val.type = ARSDK_ARG_TYPE_I64;
			res = decoder_read_i64(&dec, &val.data.i64);
			if (res < 0)
				goto out;
			*va_arg(args, int64_t *) = val.data.i64;
			break;

		case ARSDK_ARG_TYPE_U64:
			val.type = ARSDK_ARG_TYPE_U64;
			res = decoder_read_u64(&dec, &val.data.u64);
			if (res < 0)
				goto out;
			*va_arg(args, uint64_t *) = val.data.u64;
			break;

		case ARSDK_ARG_TYPE_FLOAT:
			val.type = ARSDK_ARG_TYPE_FLOAT;
			res = decoder_read_f32(&dec, &val.data.f32);
			if (res < 0)
				goto out;
			*va_arg(args, float *) = val.data.f32;
			break;

		case ARSDK_ARG_TYPE_DOUBLE:
			val.type = ARSDK_ARG_TYPE_DOUBLE;
			res = decoder_read_f64(&dec, &val.data.f64);
			if (res < 0)
				goto out;
			*va_arg(args, double *) = val.data.f64;
			break;

		case ARSDK_ARG_TYPE_STRING:
			val.type = ARSDK_ARG_TYPE_STRING;
			res = decoder_read_cstr(&dec, &val.data.cstr);
			if (res < 0)
				goto out;
			*va_arg(args, const char **) = val.data.cstr;
			break;

		case ARSDK_ARG_TYPE_ENUM:
			val.type = ARSDK_ARG_TYPE_ENUM;
			/* enum shall be extracted as i32 */
			res = decoder_read_i32(&dec, &val.data.i32);
			if (res < 0)
				goto out;
			*va_arg(args, int32_t *) = val.data.i32;
			break;

		case ARSDK_ARG_TYPE_MULTISET:
			res = decoder_read_multiset(&dec,
					va_arg(args, struct arsdk_multiset *));
			if (res < 0)
				goto out;
			break;

		default:
			res = -EINVAL;
			ARSDK_LOGW("decoder: unknown argument type %d",
					arg_desc->type);
			goto out;
		}
	}

out:
	va_end(args);
	decoder_clear(&dec);
	return res;
}

/**
 */
int arsdk_cmd_dec_header(struct arsdk_cmd *cmd)
{
	int res = 0;
	struct decoder dec;

	ARSDK_RETURN_ERR_IF_FAILED(cmd != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cmd->buf != NULL, -EINVAL);

	/* Initialize decoder */
	decoder_init(&dec, cmd->buf);

	/* Read project/class/cmd ids */
	res = decoder_read_u8(&dec, &cmd->prj_id);
	if (res < 0)
		goto out;
	res = decoder_read_u8(&dec, &cmd->cls_id);
	if (res < 0)
		goto out;
	res = decoder_read_u16(&dec, &cmd->cmd_id);
	if (res < 0)
		goto out;

	cmd->id = ARSDK_CMD_FULL_ID(cmd->prj_id, cmd->cls_id, cmd->cmd_id);

out:
	decoder_clear(&dec);
	return res;
}

/**
 */
const struct arsdk_cmd_desc *arsdk_cmd_find_desc(const struct arsdk_cmd *cmd)
{
	const struct arsdk_cmd_desc * const * const * const *project_table =
			g_arsdk_cmd_desc_table;
	const struct arsdk_cmd_desc * const * const *class_table = NULL;
	const struct arsdk_cmd_desc * const *cmd_table = NULL;
	const struct arsdk_cmd_desc *cmd_desc = NULL;
	int project_found = 0, class_found = 0;

	while (*project_table != NULL) {
		class_table = *project_table;
		while (*class_table != NULL) {
			cmd_table = *class_table;
			while (*cmd_table != NULL) {
				cmd_desc = *cmd_table;
				if (cmd_desc->prj_id != cmd->prj_id)
					goto next_product;
				project_found = 1;
				if (cmd_desc->cls_id != cmd->cls_id)
					goto next_class;
				class_found = 1;
				if (cmd_desc->cmd_id == cmd->cmd_id)
					return cmd_desc;
				cmd_table++;
			}
next_class:
			if (class_found)
				return NULL;
			class_table++;
		}
next_product:
		if (project_found)
			return NULL;
		project_table++;
	}

	return NULL;
}

int arsdk_cmd_get_values(const struct arsdk_cmd *cmd,
		struct arsdk_value *values, size_t max_count, size_t *count)
{
	int res = 0;
	uint32_t i = 0;
	struct decoder dec;
	const struct arsdk_cmd_desc *cmd_desc = NULL;
	const struct arsdk_arg_desc *arg_desc = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(cmd != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(values != NULL, -EINVAL);

	cmd_desc = arsdk_cmd_find_desc(cmd);
	if (cmd_desc == NULL)
		return -ENOENT;

	/* check that actual count is not too big */
	if (cmd_desc->arg_desc_count > max_count) {
		ULOGE("arg_count(%d) > max_count(%zd)",
				cmd_desc->arg_desc_count, max_count);
		return -EINVAL;
	}

	/* fill count */
	if (count)
		*count = cmd_desc->arg_desc_count;

	/* Initialize decoder */
	decoder_init(&dec, cmd->buf);

	/* Skip project/class/cmd ids */
	dec.off += 4;

	/* fill values */
	for (i = 0; i < cmd_desc->arg_desc_count; i++) {
		arg_desc = &cmd_desc->arg_desc_table[i];

		switch (arg_desc->type) {
		case ARSDK_ARG_TYPE_I8:
			values[i].type = ARSDK_ARG_TYPE_I8;
			res = decoder_read_i8(&dec, &values[i].data.i8);
			break;

		case ARSDK_ARG_TYPE_U8:
			values[i].type = ARSDK_ARG_TYPE_U8;
			res = decoder_read_u8(&dec, &values[i].data.u8);
			break;

		case ARSDK_ARG_TYPE_I16:
			values[i].type = ARSDK_ARG_TYPE_I16;
			res = decoder_read_i16(&dec, &values[i].data.i16);
			break;

		case ARSDK_ARG_TYPE_U16:
			values[i].type = ARSDK_ARG_TYPE_U16;
			res = decoder_read_u16(&dec, &values[i].data.u16);
			break;

		case ARSDK_ARG_TYPE_I32:
			values[i].type = ARSDK_ARG_TYPE_I32;
			res = decoder_read_i32(&dec, &values[i].data.i32);
			break;

		case ARSDK_ARG_TYPE_U32:
			values[i].type = ARSDK_ARG_TYPE_U32;
			res = decoder_read_u32(&dec, &values[i].data.u32);
			break;

		case ARSDK_ARG_TYPE_I64:
			values[i].type = ARSDK_ARG_TYPE_I64;
			res = decoder_read_i64(&dec, &values[i].data.i64);
			break;

		case ARSDK_ARG_TYPE_U64:
			values[i].type = ARSDK_ARG_TYPE_U64;
			res = decoder_read_u64(&dec, &values[i].data.u64);
			break;

		case ARSDK_ARG_TYPE_FLOAT:
			values[i].type = ARSDK_ARG_TYPE_FLOAT;
			res = decoder_read_f32(&dec, &values[i].data.f32);
			break;

		case ARSDK_ARG_TYPE_DOUBLE:
			values[i].type = ARSDK_ARG_TYPE_DOUBLE;
			res = decoder_read_f64(&dec, &values[i].data.f64);
			break;

		case ARSDK_ARG_TYPE_STRING:
			values[i].type = ARSDK_ARG_TYPE_STRING;
			res = decoder_read_cstr(&dec, &values[i].data.cstr);
			break;

		case ARSDK_ARG_TYPE_ENUM:
			/* enum shall be extracted as i32 */
			values[i].type = ARSDK_ARG_TYPE_ENUM;
			res = decoder_read_i32(&dec, &values[i].data.i32);
			break;

		case ARSDK_ARG_TYPE_MULTISET:
			values[i].type = ARSDK_ARG_TYPE_MULTISET;
			res = decoder_read_multiset(&dec,
					values[i].data.multi);
			break;

		default:
			res = -EINVAL;
			break;
		}

		if (res < 0)
			goto out;
	}

out:
	decoder_clear(&dec);
	return res;
}

/**
 */
int arsdk_cmd_fmt(const struct arsdk_cmd *cmd, char *buf, size_t len)
{
	int res = 0;
	struct decoder dec;
	const struct arsdk_cmd_desc *cmd_desc = NULL;
	const struct arsdk_arg_desc *arg_desc = NULL;
	struct arsdk_value val;
	size_t off = 0;
	uint32_t i = 0;
	const char *enum_str = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(cmd != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cmd->buf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(buf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(len >= 1, -EINVAL);

	/* Find command description */
	cmd_desc = arsdk_cmd_find_desc(cmd);
	if (cmd_desc == NULL) {
		snprintf(buf, len, "Unknown %u.%u.%u",
				cmd->prj_id, cmd->cls_id, cmd->cmd_id);
		return 0;
	}

	/* Initialize decoder */
	decoder_init(&dec, cmd->buf);

	/* Skip project/class/cmd ids */
	dec.off += 4;

	/* Command name */
	fmt_append(buf, len, &off, "%s", cmd_desc->name);

	/* Arguments */
	for (i = 0; i < cmd_desc->arg_desc_count; i++) {
		arg_desc = &cmd_desc->arg_desc_table[i];
		fmt_append(buf, len, &off, " | %s=", arg_desc->name);
		switch (arg_desc->type) {
		case ARSDK_ARG_TYPE_I8:
			val.type = ARSDK_ARG_TYPE_I8;
			res = decoder_read_i8(&dec, &val.data.i8);
			if (res < 0)
				goto out;
			if (arg_desc->enum_desc_table)
				fmt_append_bitfield(buf,
						    len,
						    &off,
						    arg_desc,
						    (uint64_t)val.data.i8);
			else
				fmt_append(buf, len, &off, "%d", val.data.i8);
			break;

		case ARSDK_ARG_TYPE_U8:
			val.type = ARSDK_ARG_TYPE_U8;
			res = decoder_read_u8(&dec, &val.data.u8);
			if (res < 0)
				goto out;
			if (arg_desc->enum_desc_table)
				fmt_append_bitfield(buf,
						    len,
						    &off,
						    arg_desc,
						    (uint64_t)val.data.u8);
			else
				fmt_append(buf, len, &off, "%u", val.data.u8);
			break;

		case ARSDK_ARG_TYPE_I16:
			val.type = ARSDK_ARG_TYPE_I16;
			res = decoder_read_i16(&dec, &val.data.i16);
			if (res < 0)
				goto out;
			if (arg_desc->enum_desc_table)
				fmt_append_bitfield(buf,
						    len,
						    &off,
						    arg_desc,
						    (uint64_t)val.data.i16);
			else
				fmt_append(buf, len, &off, "%d", val.data.i16);
			break;

		case ARSDK_ARG_TYPE_U16:
			val.type = ARSDK_ARG_TYPE_U16;
			res = decoder_read_u16(&dec, &val.data.u16);
			if (res < 0)
				goto out;
			if (arg_desc->enum_desc_table)
				fmt_append_bitfield(buf,
						    len,
						    &off,
						    arg_desc,
						    (uint64_t)val.data.u16);
			else
				fmt_append(buf, len, &off, "%u", val.data.u16);
			break;

		case ARSDK_ARG_TYPE_I32:
			val.type = ARSDK_ARG_TYPE_I32;
			res = decoder_read_i32(&dec, &val.data.i32);
			if (res < 0)
				goto out;
			if (arg_desc->enum_desc_table)
				fmt_append_bitfield(buf,
						    len,
						    &off,
						    arg_desc,
						    (uint64_t)val.data.i32);
			else
				fmt_append(buf, len, &off, "%d", val.data.i32);
			break;

		case ARSDK_ARG_TYPE_U32:
			val.type = ARSDK_ARG_TYPE_U32;
			res = decoder_read_u32(&dec, &val.data.u32);
			if (res < 0)
				goto out;
			if (arg_desc->enum_desc_table)
				fmt_append_bitfield(buf,
						    len,
						    &off,
						    arg_desc,
						    (uint64_t)val.data.u32);
			else
				fmt_append(buf, len, &off, "%u", val.data.u32);
			break;

		case ARSDK_ARG_TYPE_I64:
			val.type = ARSDK_ARG_TYPE_I64;
			res = decoder_read_i64(&dec, &val.data.i64);
			if (res < 0)
				goto out;
			if (arg_desc->enum_desc_table)
				fmt_append_bitfield(buf,
						    len,
						    &off,
						    arg_desc,
						    (uint64_t)val.data.i64);
			else
				fmt_append(buf, len, &off,
					   "%" PRIi64, val.data.i64);
			break;

		case ARSDK_ARG_TYPE_U64:
			val.type = ARSDK_ARG_TYPE_U64;
			res = decoder_read_u64(&dec, &val.data.u64);
			if (res < 0)
				goto out;
			if (arg_desc->enum_desc_table)
				fmt_append_bitfield(buf,
						    len,
						    &off,
						    arg_desc,
						    val.data.u64);
			else
				fmt_append(buf, len, &off,
					   "%" PRIu64, val.data.u64);
			break;

		case ARSDK_ARG_TYPE_FLOAT:
			val.type = ARSDK_ARG_TYPE_FLOAT;
			res = decoder_read_f32(&dec, &val.data.f32);
			if (res < 0)
				goto out;
			fmt_append(buf, len, &off, "%f", val.data.f32);
			break;

		case ARSDK_ARG_TYPE_DOUBLE:
			val.type = ARSDK_ARG_TYPE_DOUBLE;
			res = decoder_read_f64(&dec, &val.data.f64);
			if (res < 0)
				goto out;
			fmt_append(buf, len, &off, "%f", val.data.f64);
			break;

		case ARSDK_ARG_TYPE_STRING:
			val.type = ARSDK_ARG_TYPE_STRING;
			res = decoder_read_cstr(&dec, &val.data.cstr);
			if (res < 0)
				goto out;
			fmt_append(buf, len, &off, "'%s'", val.data.cstr);
			break;

		case ARSDK_ARG_TYPE_ENUM:
			val.type = ARSDK_ARG_TYPE_ENUM;
			/* enum shall be extracted as i32 */
			res = decoder_read_i32(&dec, &val.data.i32);
			if (res < 0)
				goto out;
			enum_str = get_enum_str(arg_desc, val.data.i32);
			if (enum_str != NULL) {
				fmt_append(buf, len, &off, "%s", enum_str);
			} else {
				fmt_append(buf, len, &off,
						"UNKNOWN(%d)", val.data.i32);
			}
			break;

		default:
			res = -EINVAL;
			ARSDK_LOGW("decoder: unknown argument type %d",
					arg_desc->type);
			goto out;
		}
	}

out:
	decoder_clear(&dec);
	return res;
}
