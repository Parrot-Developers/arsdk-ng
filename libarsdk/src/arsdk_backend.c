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


/**
 */
const char *arsdk_socket_kind_str(enum arsdk_socket_kind val)
{
	switch (val) {
	case ARSDK_SOCKET_KIND_DISCOVERY: return "DISCOVERY";
	case ARSDK_SOCKET_KIND_CONNECTION: return "CONNECTION";
	case ARSDK_SOCKET_KIND_COMMAND: return "COMMAND";
	case ARSDK_SOCKET_KIND_FTP: return "FTP";
	case ARSDK_SOCKET_KIND_VIDEO: return "VIDEO";
	default: return "UNKNOWN";
	}
}

/**
 */
const char *arsdk_device_type_str(enum arsdk_device_type val)
{
	switch (val) {
	case ARSDK_DEVICE_TYPE_BEBOP: return "BEBOP";
	case ARSDK_DEVICE_TYPE_BEBOP_2: return "BEBOP_2";
	case ARSDK_DEVICE_TYPE_PAROS: return "PAROS";
	case ARSDK_DEVICE_TYPE_ANAFI4K: return "ANAFI4K";
	case ARSDK_DEVICE_TYPE_ANAFI_THERMAL: return "ANAFI_THERMAL";
	case ARSDK_DEVICE_TYPE_CHIMERA: return "CHIMERA";
	case ARSDK_DEVICE_TYPE_ANAFI_2: return "ANAFI_2";
	case ARSDK_DEVICE_TYPE_ANAFI_UA: return "ANAFI_UA";
	case ARSDK_DEVICE_TYPE_ANAFI_USA: return "ANAFI_USA";
	case ARSDK_DEVICE_TYPE_SKYCTRL: return "SKYCTRL";
	case ARSDK_DEVICE_TYPE_SKYCTRL_2: return "SKYCTRL_2";
	case ARSDK_DEVICE_TYPE_SKYCTRL_2P: return "SKYCTRL_2P";
	case ARSDK_DEVICE_TYPE_SKYCTRL_NG: return "SKYCTRL_NG";
	case ARSDK_DEVICE_TYPE_SKYCTRL_3: return "SKYCTRL_3";
	case ARSDK_DEVICE_TYPE_SKYCTRL_UA: return "SKYCTRL_UA";
	case ARSDK_DEVICE_TYPE_SKYCTRL_4: return "SKYCTRL_4";
	case ARSDK_DEVICE_TYPE_JS: return "JS";
	case ARSDK_DEVICE_TYPE_JS_EVO_LIGHT: return "JS_EVO_LIGHT";
	case ARSDK_DEVICE_TYPE_JS_EVO_RACE: return "JS_EVO_RACE";
	case ARSDK_DEVICE_TYPE_RS: return "RS";
	case ARSDK_DEVICE_TYPE_RS_EVO_LIGHT: return "RS_EVO_LIGHT";
	case ARSDK_DEVICE_TYPE_RS_EVO_BRICK: return "RS_EVO_BRICK";
	case ARSDK_DEVICE_TYPE_RS_EVO_HYDROFOIL: return "RS_EVO_HYDROFOIL";
	case ARSDK_DEVICE_TYPE_RS3: return "RS3";
	case ARSDK_DEVICE_TYPE_POWERUP: return "POWERUP";
	case ARSDK_DEVICE_TYPE_EVINRUDE: return "EVINRUDE";
	case ARSDK_DEVICE_TYPE_WINGX: return "WINGX";
	case ARSDK_DEVICE_TYPE_UNKNOWN: /* NO BREAK */
	default: return "UNKNOWN";
	}
}

/**
 */
const char *arsdk_conn_cancel_reason_str(enum arsdk_conn_cancel_reason val)
{
	switch (val) {
	case ARSDK_CONN_CANCEL_REASON_LOCAL: return "LOCAL";
	case ARSDK_CONN_CANCEL_REASON_REMOTE: return "REMOTE";
	case ARSDK_CONN_CANCEL_REASON_REJECTED: return "REJECTED";
	default: return "UNKNOWN";
	}
}

/**
 */
const char *arsdk_link_status_str(enum arsdk_link_status val)
{
	switch (val) {
	case ARSDK_LINK_STATUS_KO: return "KO";
	case ARSDK_LINK_STATUS_OK: return "OK";
	default: return "UNKNOWN";
	}
}

/**
 */
const char *arsdk_backend_type_str(enum arsdk_backend_type val)
{
	switch (val) {
	case ARSDK_BACKEND_TYPE_NET: return "NET";
	case ARSDK_BACKEND_TYPE_BLE: return "BLE";
	case ARSDK_BACKEND_TYPE_MUX: return "MUX";
	case ARSDK_BACKEND_TYPE_UNKNOWN: /* NO BREAK */
	default: return "UNKNOWN";
	}
}

/**
 */
int arsdk_backend_new(void *child, struct arsdk_mngr *mngr, const char *name,
		enum arsdk_backend_type type,
		const struct arsdk_backend_ops *ops,
		struct arsdk_backend **ret_obj)
{
	struct arsdk_backend *self = NULL;
	int ret;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(ops != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(mngr != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(name != NULL, -EINVAL);

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Initialize structure */
	self->child = child;
	self->ops = ops;
	self->name = strdup(name);
	self->type = type;
	self->mngr = mngr;

	/* register backend in manager */
	ret = arsdk_mngr_register_backend(self->mngr, self);
	if (ret < 0) {
		free(self->name);
		free(self);
		return ret;
	}

	*ret_obj = self;
	return 0;
}

/**
 */
int arsdk_backend_destroy(struct arsdk_backend *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	/* unregister backend from manager */
	arsdk_mngr_unregister_backend(self->mngr, self);
	free(self->name);
	free(self);
	return 0;
}

/**
 */
void *arsdk_backend_get_child(struct arsdk_backend *self)
{
	return self == NULL ? NULL : self->child;
}

/**
 */
int arsdk_backend_set_osdata(struct arsdk_backend *self, void *osdata)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	self->osdata = osdata;
	return 0;
}

/**
 */
void *arsdk_backend_get_osdata(struct arsdk_backend *self)
{
	return self == NULL ? NULL : self->osdata;
}

/**
 */
const char *arsdk_backend_get_name(struct arsdk_backend *self)
{
	return self ? self->name : NULL;
}

/**
 */
enum arsdk_backend_type arsdk_backend_get_type(struct arsdk_backend *self)
{
	return self ? self->type : ARSDK_BACKEND_TYPE_UNKNOWN;
}

/**
 */
int arsdk_backend_create_peer(struct arsdk_backend *self,
		const struct arsdk_peer_info *info,
		struct arsdk_peer_conn *conn,
		struct arsdk_peer **ret_obj)
{

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	return arsdk_mngr_create_peer(self->mngr, self, info, conn, ret_obj);
}

/**
 */
int arsdk_backend_destroy_peer(struct arsdk_backend *self,
		struct arsdk_peer *peer)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	return arsdk_mngr_destroy_peer(self->mngr, peer);
}

/**
 */
int arsdk_backend_accept_peer_conn(struct arsdk_backend *self,
		struct arsdk_peer *peer,
		struct arsdk_peer_conn *conn,
		const struct arsdk_peer_conn_cfg *cfg,
		const struct arsdk_peer_conn_internal_cbs *cbs,
		struct pomp_loop *loop)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	if (self->ops->accept_peer_conn == NULL)
		return -ENOSYS;
	return (*self->ops->accept_peer_conn)(self, peer, conn, cfg, cbs, loop);
}

/**
 */
int arsdk_backend_reject_peer_conn(struct arsdk_backend *self,
		struct arsdk_peer *peer,
		struct arsdk_peer_conn *conn)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	if (self->ops->reject_peer_conn == NULL)
		return -ENOSYS;
	return (*self->ops->reject_peer_conn)(self, peer, conn);
}

/**
 */
int arsdk_backend_stop_peer_conn(struct arsdk_backend *self,
		struct arsdk_peer *peer,
		struct arsdk_peer_conn *conn)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	if (self->ops->stop_peer_conn == NULL)
		return -ENOSYS;
	return (*self->ops->stop_peer_conn)(self, peer, conn);
}

int arsdk_backend_socket_cb(struct arsdk_backend *self, int fd,
		enum arsdk_socket_kind kind)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	/* socket hook callback */
	if (self->ops->socket_cb != NULL)
		(*self->ops->socket_cb)(self, fd, kind);

	return 0;
}
