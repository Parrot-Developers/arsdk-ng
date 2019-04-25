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

#include "arsdkctrl_priv.h"

#include "arsdk_blackbox_itf_priv.h"
#include "arsdkctrl_default_log.h"
#ifdef BUILD_LIBMUX
#include "mux/arsdk_mux.h"
#endif /* BUILD_LIBMUX */

/** */
struct arsdk_blackbox_itf {
	struct mux_ctx                          *mux;
	struct list_node                        listeners;
};

/** */
struct arsdk_blackbox_listener {
	struct arsdk_blackbox_itf               *itf;
	struct arsdk_blackbox_listener_cbs      cbs;
	struct list_node                        node;
};

#ifdef BUILD_LIBMUX
static int dec_button_action(struct arsdk_blackbox_itf *itf,
		struct pomp_msg *msg)
{
	int res;
	int8_t action;
	struct arsdk_blackbox_listener *listener = NULL;
	struct arsdk_blackbox_listener *listener_tmp = NULL;

	/* decode button action */
	res = pomp_msg_read(msg, MUX_BLACKBOX_MSG_FMT_DEC_BUTTON_ACTION,
			&action);
	if (res < 0) {
		ARSDK_LOG_ERRNO("pomp_msg_read failed", -res);
		return res;
	}

	/* notify button action */
	list_walk_entry_forward_safe(&itf->listeners, listener, listener_tmp,
			node) {
		(*listener->cbs.rc_button_action)(itf, listener, action,
				listener->cbs.userdata);
	}

	return 0;
}

static int dec_piloting_info(struct arsdk_blackbox_itf *itf,
		struct pomp_msg *msg)
{
	int res;
	struct arsdk_blackbox_rc_piloting_info info;
	struct arsdk_blackbox_listener *listener = NULL;
	struct arsdk_blackbox_listener *listener_tmp = NULL;

	/* decode piloting info */
	res = pomp_msg_read(msg, MUX_BLACKBOX_MSG_FMT_DEC_PILOTING_INFO,
			&info.source, &info.roll, &info.pitch, &info.yaw,
			&info.gaz);
	if (res < 0) {
		ARSDK_LOG_ERRNO("pomp_msg_read failed", -res);
		return res;
	}

	/* notify piloting info */
	list_walk_entry_forward_safe(&itf->listeners, listener, listener_tmp,
			node) {
		(*listener->cbs.rc_piloting_info)(itf, listener, &info,
				listener->cbs.userdata);
	}

	return 0;
}

static void blackbox_mux_channel_recv(struct arsdk_blackbox_itf *itf,
		struct pomp_buffer *buf)
{
	struct pomp_msg *msg = NULL;

	ARSDK_RETURN_IF_FAILED(itf != NULL, -EINVAL);
	ARSDK_RETURN_IF_FAILED(buf != NULL, -EINVAL);

	/* Create pomp message from buffer */
	msg = pomp_msg_new_with_buffer(buf);
	if (msg == NULL)
		return;

	/* Decode message */
	switch (pomp_msg_get_id(msg)) {
	case MUX_BLACKBOX_MSG_ID_BUTTON_ACTION:
		/* decode button action */
		dec_button_action(itf, msg);
		break;

	case MUX_BLACKBOX_MSG_ID_PILOTING_INFO:
		/* decode piloting info */
		dec_piloting_info(itf, msg);
		break;

	default:
		ARSDK_LOGE("unsupported blackbox mux msg %d",
				pomp_msg_get_id(msg));
		break;
	}

	pomp_msg_destroy(msg);
	return;
}

static void blackbox_mux_channel_cb(struct mux_ctx *ctx, uint32_t chanid,
		enum mux_channel_event event, struct pomp_buffer *buf,
		void *userdata)
{
	struct arsdk_blackbox_itf *itf = userdata;

	ARSDK_RETURN_IF_FAILED(itf != NULL, -EINVAL);

	switch (event) {
	case MUX_CHANNEL_RESET:
		/* do nothing*/
		break;
	case MUX_CHANNEL_DATA:
		blackbox_mux_channel_recv(itf, buf);
		break;
	}
}
#endif /* BUILD_LIBMUX */

int arsdk_blackbox_itf_new(struct mux_ctx *mux,
		struct arsdk_blackbox_itf **ret_obj)
{
	int res = 0;
	struct arsdk_blackbox_itf *itf = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;

	/* Allocate structure */
	itf = calloc(1, sizeof(*itf));
	if (itf == NULL)
		return -ENOMEM;

	/* Initialize structure */
	list_init(&itf->listeners);
	if (mux != NULL) {
#ifdef BUILD_LIBMUX
		itf->mux = mux;
		mux_ref(itf->mux);

		/* open mux update channel */
		res = mux_channel_open(itf->mux,
				MUX_BLACKBOX_CHANNEL_ID_BLACKBOX,
				&blackbox_mux_channel_cb, itf);
		if (res < 0)
			goto error;
#else /* !BUILD_LIBMUX */
		res = -ENOSYS;
		goto error;
#endif /* !BUILD_LIBMUX */
	}

	/* Success */
	*ret_obj = itf;
	return 0;

	/* Cleanup in case of error */
error:
	arsdk_blackbox_itf_destroy(itf);
	return res;
}

static int arsdk_blackbox_itf_unregistrer_all(struct arsdk_blackbox_itf *itf)
{
	struct arsdk_blackbox_listener *listener = NULL;
	struct arsdk_blackbox_listener *listener_tmp = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	list_walk_entry_forward_safe(&itf->listeners, listener, listener_tmp,
			node) {
		arsdk_blackbox_listener_unregister(listener);
	}

	return 0;
}

int arsdk_blackbox_itf_stop(struct arsdk_blackbox_itf *itf)
{
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	arsdk_blackbox_itf_unregistrer_all(itf);

#ifdef BUILD_LIBMUX
	if (itf->mux)
		mux_channel_close(itf->mux, MUX_BLACKBOX_CHANNEL_ID_BLACKBOX);
#endif /* !BUILD_LIBMUX */

	return 0;
}

int arsdk_blackbox_itf_destroy(struct arsdk_blackbox_itf *itf)
{
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);

	arsdk_blackbox_itf_stop(itf);
#ifdef BUILD_LIBMUX
	if (itf->mux)
		mux_unref(itf->mux);
#endif /* !BUILD_LIBMUX */
	free(itf);
	return 0;
}

int arsdk_blackbox_itf_create_listener(struct arsdk_blackbox_itf *itf,
		struct arsdk_blackbox_listener_cbs *cbs,
		struct arsdk_blackbox_listener **ret_obj)
{
	struct arsdk_blackbox_listener *listener = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(itf != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);

	/* Allocate structure */
	listener = calloc(1, sizeof(*listener));
	if (listener == NULL)
		return -ENOMEM;

	/* Initialize structure */
	listener->itf = itf;
	listener->cbs = *cbs;

	/* Success */
	list_add_after(&itf->listeners, &listener->node);
	*ret_obj = listener;
	return 0;
}

int arsdk_blackbox_listener_unregister(struct arsdk_blackbox_listener *listener)
{
	ARSDK_RETURN_ERR_IF_FAILED(listener != NULL, -EINVAL);

	/* notify unregister */
	(*listener->cbs.unregister)(listener->itf, listener,
			listener->cbs.userdata);

	/* delete listener */
	list_del(&listener->node);
	free(listener);

	return 0;
}
