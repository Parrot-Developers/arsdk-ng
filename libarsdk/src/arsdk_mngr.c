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

struct arsdk_mngr {
	/* running loop */
	struct pomp_loop              *loop;
	/* os specific user data */
	void                          *osdata;
	/* peer callbacks */
	struct arsdk_mngr_peer_cbs    peer_cbs;
	/* peers list */
	struct list_node              peers;
	/* backends list */
	struct list_node              backends;
};

/**
 */
int arsdk_mngr_new(struct pomp_loop *loop, struct arsdk_mngr **ret_mngr)
{
	struct arsdk_mngr *mngr = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(loop != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_mngr != NULL, -EINVAL);
	*ret_mngr = NULL;

	/* Allocate structure */
	mngr = calloc(1, sizeof(*mngr));
	if (mngr == NULL)
		return -ENOMEM;

	srandom(time(NULL));

	/* Initialize parameters */
	mngr->loop = loop;
	list_init(&mngr->peers);
	list_init(&mngr->backends);

	*ret_mngr = mngr;
	return 0;
}

/**
 */
int arsdk_mngr_set_peer_cbs(struct arsdk_mngr *self,
		const struct arsdk_mngr_peer_cbs *cbs)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->added != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->removed != NULL, -EINVAL);
	self->peer_cbs = *cbs;
	return 0;
}

struct pomp_loop *arsdk_mngr_get_loop(struct arsdk_mngr *mngr)
{
	return mngr ? mngr->loop : NULL;
}

/**
 */
int arsdk_mngr_destroy(struct arsdk_mngr *self)
{
	struct arsdk_backend *backend, *backendtmp;
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	/* unregister all backends */
	list_walk_entry_forward_safe(&self->backends, backend, backendtmp,
			node) {
		arsdk_mngr_unregister_backend(self, backend);
	}

	free(self);
	return 0;
}

/**
 */
static int arsdk_mngr_register_peer(struct arsdk_mngr *self,
		struct arsdk_peer *peer)
{
	struct arsdk_peer *p;

	/* first check peer is not already in the list */
	list_walk_entry_forward(&self->peers, p, node) {
		if (p == peer) {
			ARSDK_LOGW("can't add peer %p: already added !", peer);
			return -EEXIST;
		}
	}

	/* append peer in device list */
	list_add_before(&self->peers, &peer->node);

	/* notify callback */
	if (self->peer_cbs.added)
		(*self->peer_cbs.added) (peer, self->peer_cbs.userdata);

	return 0;
}

/**
 */
static int arsdk_mngr_unregister_peer(struct arsdk_mngr *self,
		struct arsdk_peer *peer)
{
	struct arsdk_peer *p;
	int found = 0;

	/* check peer is in the list */
	list_walk_entry_forward(&self->peers, p, node) {
		if (p == peer) {
			found = 1;
			break;
		}
	}

	if (!found) {
		ARSDK_LOGW("can't remove device %p: not added !", peer);
		return -ENOENT;
	}

	/* remove device from list */
	list_del(&peer->node);

	/* notify callback */
	if (self->peer_cbs.removed)
		(*self->peer_cbs.removed) (peer, self->peer_cbs.userdata);

	return 0;
}

/**
 */
struct arsdk_peer *
arsdk_mngr_next_peer(struct arsdk_mngr *mngr, struct arsdk_peer *prev)
{
	struct list_node *node;
	struct arsdk_peer *next;

	if (!mngr)
		return NULL;

	node = list_next(&mngr->peers, prev ? &prev->node : &mngr->peers);
	if (!node)
		return NULL;

	next = list_entry(node, struct arsdk_peer, node);
	return next;
}


static uint16_t arsdk_mngr_generate_peer_handle(struct arsdk_mngr *self)
{
	struct arsdk_peer *peer;
	int collision;
	uint16_t handle;

	while (1) {
		/* generate random handle */
		handle = (uint16_t)random();
		if (handle == ARSDK_INVALID_HANDLE)
			continue;

		/* check for peer handle collision */
		collision = 0;
		list_walk_entry_forward(&self->peers, peer, node) {
			if (arsdk_peer_get_handle(peer) == handle) {
				collision = 1;
				break;
			}
		}

		if (!collision)
			break;
	}

	return handle;
}

/**
 */
int arsdk_mngr_create_peer(struct arsdk_mngr *self,
		struct arsdk_backend *backend,
		const struct arsdk_peer_info *info,
		struct arsdk_peer_conn *conn,
		struct arsdk_peer **ret_obj)
{
	struct arsdk_peer *peer = NULL;
	uint16_t handle;
	int res;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(backend != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(info != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;

	/* generate a new handle */
	handle = arsdk_mngr_generate_peer_handle(self);

	/* create the peer */
	res = arsdk_peer_new(backend, info, handle, conn, &peer);
	if (res < 0)
		return res;

	/* register peer in manager */
	res = arsdk_mngr_register_peer(self, peer);
	if (res < 0) {
		arsdk_peer_destroy(peer);
		return res;
	}

	*ret_obj = peer;
	return 0;
}


int arsdk_mngr_destroy_peer(struct arsdk_mngr *self,
		struct arsdk_peer *peer)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(peer != NULL, -EINVAL);

	if (peer->deleting)
		return 0;

	/* mark peer has deleting to avoid reentrancy from
	 * arsdk_peer_disconnect function */
	peer->deleting = 1;

	/* disconnect peer if needed */
	arsdk_peer_disconnect(peer);

	/* unregister it from manager */
	arsdk_mngr_unregister_peer(self, peer);

	/* destroy peer */
	arsdk_peer_destroy(peer);

	return 0;
}

int arsdk_mngr_register_backend(struct arsdk_mngr *self,
		struct arsdk_backend *backend)
{
	struct arsdk_backend *b;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(backend != NULL, -EINVAL);

	/* first check backend is not already in the list */
	list_walk_entry_forward(&self->backends, b, node) {
		if (b == backend) {
			ARSDK_LOGW("can't register backend %p:"
				"already registered !", backend);
			return -EEXIST;
		}
	}

	/* add backend in list */
	list_add_before(&self->backends, &backend->node);
	return 0;
}

int arsdk_mngr_unregister_backend(struct arsdk_mngr *self,
		struct arsdk_backend *backend)
{
	struct arsdk_backend *b;
	struct arsdk_peer *peer, *tmppeer;
	int found = 0;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(backend != NULL, -EINVAL);

	/* check backend is in the list */
	list_walk_entry_forward(&self->backends, b, node) {
		if (b == backend) {
			found = 1;
			break;
		}
	}

	if (!found) {
		ARSDK_LOGW("can't unregister backend %p: not registered !",
				backend);
		return -ENOENT;
	}

	/* remove all peers from backend */
	list_walk_entry_forward_safe(&self->peers, peer, tmppeer, node) {
		if (peer->backend == backend)
			arsdk_mngr_destroy_peer(self, peer);
	}

	/* remove backend from list */
	list_del(&backend->node);

	return 0;
}

struct arsdk_peer *arsdk_mngr_get_peer(struct arsdk_mngr *self,
		uint16_t handle)
{
	struct arsdk_peer *peer;

	if (!self || handle == ARSDK_INVALID_HANDLE)
		return NULL;

	list_walk_entry_forward(&self->peers, peer, node) {
		if (arsdk_peer_get_handle(peer) == handle)
			return peer;
	}

	return NULL;
}
