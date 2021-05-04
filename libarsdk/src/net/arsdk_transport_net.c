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
#include "arsdk_net.h"
#include "arsdk_net_log.h"

#define ARSDK_FRAME_V1_HEADER_SIZE      7
#define ARSDK_FRAME_V2_HEADER_SIZE_MIN  6
#define ARSDK_FRAME_V2_HEADER_SIZE_MAX  14
#define ARSDK_TRANSPORT_PING_PERIOD     2000
#define ARSDK_TRANSPORT_TAG             "net"

/**
 * Determine if a read/write error in non-blocking could not be completed.
 * POSIX.1-2001 allows either error to be returned for this case, and
 * does not require these constants to have the same value, so a portable
 * application should check for both possibilities. */
#define ARSDK_WOULD_BLOCK(_err) \
	( \
		(_err) == EAGAIN || \
		(_err) == EWOULDBLOCK \
	)

/**
 */
struct socket {
	int                     fd;
	in_addr_t               *txaddr;
	uint16_t                *rxport;
	uint16_t                *txport;
	void                    *rxbuf;
	size_t                  rxbufsize;
	int                     rxenabled;
	int                     txenabled;
	enum arsdk_socket_kind  kind;
};

/** */
struct arsdk_transport_net {
	struct arsdk_transport          *parent;
	struct pomp_loop                *loop;
	int                             started;
	struct arsdk_transport_net_cfg  cfg;
	struct arsdk_transport_net_cbs  cbs;
	struct socket                   data_sock;

	/* For test/debug, ratio (percentage) of packets to drop */
	int                             rx_drop_ratio;
	int                             tx_drop_ratio;
	int                             tx_fail;
};

/**
 */
static int setup_fd_flags(int fd)
{
	int res = 0;
	if (fcntl(fd, F_SETFD, FD_CLOEXEC | fcntl(fd, F_GETFD)) < 0) {
		res = -errno;
		ARSDK_LOG_FD_ERRNO("fcntl.F_SETFD", fd, errno);
		return res;
	}
	if (fcntl(fd, F_SETFL, O_NONBLOCK | fcntl(fd, F_GETFL)) < 0) {
		res = -errno;
		ARSDK_LOG_FD_ERRNO("fcntl.F_SETFL", fd, errno);
		return res;
	}
	return 0;
}

/**
 */
static int socket_setup(struct arsdk_transport_net *self,
		struct socket *sock,
		enum arsdk_socket_kind kind)
{
	int res = 0;
	socklen_t addrlen = 0, optlen = 0;
	uint32_t buflen = 0;
	struct sockaddr_in addr;
	uint16_t newrxport = 0;

	/* Nothing to do if neither rx nor tx is enabled */
	if (!sock->rxenabled && !sock->txenabled)
		return 0;

	/* Create socket fd */
	sock->fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock->fd < 0) {
		res = -errno;
		ARSDK_LOG_ERRNO("socket", errno);
		goto error;
	}
	sock->kind = kind;

	res = setup_fd_flags(sock->fd);
	if (res < 0)
		goto error;

	if (sock->rxenabled) {
		/* Setup rx address */
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(*sock->rxport);

		/* Bind to address */
retry_bind:
		if (bind(sock->fd, (const struct sockaddr *)&addr,
				sizeof(addr)) < 0) {
			res = -errno;
			if (res == -EADDRINUSE && addr.sin_port != 0) {
				addr.sin_port = 0;
				goto retry_bind;
			}

			ARSDK_LOG_FD_ERRNO("bind", sock->fd, errno);
			goto error;
		}

		/* Retrieve back address to determine bound port if it was
		 * dynamically allocated */
		addrlen = sizeof(addr);
		if (getsockname(sock->fd, (struct sockaddr *)&addr,
				&addrlen) < 0) {
			res = -errno;
			ARSDK_LOG_FD_ERRNO("getsockname", sock->fd, errno);
			goto error;
		}
		newrxport = ntohs(addr.sin_port);
		if (newrxport != *sock->rxport) {
			ARSDK_LOGI("socket %p (%d): use dynamic port %u (%u)",
					sock, sock->fd,
					newrxport, *sock->rxport);
		}
		*sock->rxport = newrxport;

		/* Set Rx buffer size */
		buflen = 65536;
		optlen = sizeof(buflen);
		if (setsockopt(sock->fd, SOL_SOCKET, SO_RCVBUF,
				&buflen, optlen) < 0) {
			res = -errno;
			ARSDK_LOG_FD_ERRNO("setsockopt.SO_RCVBUF",
					sock->fd, errno);
			goto error;
		}

		/* Determine receive buffer size. The kernel doubles the size
		 * we put on setsockopt, so we divide by two to get the usable
		 * size */
		optlen = sizeof(sock->rxbufsize);
		if (getsockopt(sock->fd, SOL_SOCKET, SO_RCVBUF,
				&sock->rxbufsize, &optlen) < 0) {
			res = -errno;
			ARSDK_LOG_FD_ERRNO("getsockopt.SO_RCVBUF",
					sock->fd, errno);
			goto error;
		}
#ifndef _WIN32
		sock->rxbufsize /= 2;
#endif /* !_WIN32 */

		/* Allocate rx buffer */
		sock->rxbuf = malloc(sock->rxbufsize);
		if (sock->rxbuf == NULL) {
			res = -ENOMEM;
			goto error;
		}
	}

	if (sock->txenabled) {
		/* Set Tx buffer size */
		buflen = 65536;
		optlen = sizeof(buflen);
		if (setsockopt(sock->fd, SOL_SOCKET, SO_SNDBUF,
				&buflen, optlen) < 0) {
			res = -errno;
			ARSDK_LOG_FD_ERRNO("setsockopt.SO_SNDBUF",
					sock->fd, errno);
			goto error;
		}
	}

	/* Success */
	self->cbs.socketcb(self, sock->fd, kind, self->cbs.userdata);
	return 0;

	/* Cleanup in case of error */
error:
	free(sock->rxbuf);
	sock->rxbuf = NULL;
	if (sock->fd >= 0) {
		close(sock->fd);
		sock->fd = -1;
	}
	return res;
}

/**
 */
static int socket_start(struct arsdk_transport_net *self,
		struct socket *sock,
		pomp_fd_event_cb_t cb)
{
	int res = 0;
	int tos = 0;

	/* Monitor IN events of rx socket */
	if (sock->rxenabled) {
		res = pomp_loop_add(self->loop, sock->fd,
				POMP_FD_EVENT_IN, cb, self);
		if (res < 0) {
			ARSDK_LOG_ERRNO("pomp_loop_add", -res);
			goto out;
		}
	}

#ifdef _WIN32
#  define IPTOS_PREC_INTERNETCONTROL	0xc0
#  define IPTOS_PREC_FLASHOVERRIDE	0x80
#endif /* _WIN32 */
	if (self->cfg.qos_mode == 1) {
		switch (sock->kind) {
		case ARSDK_SOCKET_KIND_COMMAND:
			tos = IPTOS_PREC_INTERNETCONTROL; /* CS_6 */
			break;
		case ARSDK_SOCKET_KIND_VIDEO:
			tos = IPTOS_PREC_FLASHOVERRIDE; /* CS_4 */
			break;
		default:
			tos = 0;
			break;
		}

		if (tos != 0 && setsockopt(sock->fd, IPPROTO_IP, IP_TOS,
				&tos, sizeof(tos)) < 0) {
			res = -errno;
			ARSDK_LOG_FD_ERRNO("setsockopt.IP_TOS", sock->fd, -res);
			goto out;
		}
	}

out:
	return res;
}

/**
 */
static int socket_stop(struct arsdk_transport_net *self,
		struct socket *sock)
{
	/* Stop monitoring IN events */
	if (sock->rxenabled)
		pomp_loop_remove(self->loop, sock->fd);
	return 0;
}

/**
 */
static int socket_cleanup(struct arsdk_transport_net *self,
		struct socket *sock)
{
	if (sock->fd >= 0) {
		if (self->started)
			socket_stop(self, sock);
		close(sock->fd);
		sock->fd = -1;
	}

	if (sock->rxenabled) {
		free(sock->rxbuf);
		sock->rxbuf = NULL;
		sock->rxbufsize = 0;
	}

	return 0;
}

/**
 */
static ssize_t socket_read(struct arsdk_transport_net *self,
		struct socket *sock, int check_link_status)
{
	int res = 0;
	ssize_t readlen = 0;
	enum arsdk_link_status link_status = ARSDK_LINK_STATUS_KO;

	/* Read data, ignoring interrupts */
	do {
		readlen = recvfrom(sock->fd, sock->rxbuf, sock->rxbufsize,
				0, NULL, 0);
	} while (readlen < 0 && errno == -EINTR);

	/* Something read ? */
	if (readlen > 0) {
		if (self->rx_drop_ratio != 0 &&
				rand() % 100 < self->rx_drop_ratio) {
			ARSDK_LOGI("transport_net %p: fd=%d rx drop %zu bytes",
					self, sock->fd, readlen);
			return -EAGAIN;
		}
		return readlen;
	}

	/* EOF ? */
	if (readlen == 0) {
		ARSDK_LOGI("transport_net %p: EOF on fd=%d", self, sock->fd);
		return 0;
	}

	/* Only print error if link status is currently OK (and checked) */
	res = -errno;
	link_status = arsdk_transport_get_link_status(self->parent);
	if (!ARSDK_WOULD_BLOCK(-res) && (!check_link_status ||
			link_status == ARSDK_LINK_STATUS_OK)) {
		ARSDK_LOG_FD_ERRNO("read", sock->fd, -res);
		if (check_link_status) {
			arsdk_transport_set_link_status(self->parent,
					ARSDK_LINK_STATUS_KO);
		}
	}
	return res;
}

/**
 */
#ifdef _WIN32
static ssize_t socket_write(struct arsdk_transport_net *self,
		struct socket *sock,
		WSABUF *wsabufs, uint32_t wsabufcnt, size_t total)
{
	struct sockaddr_in addr;
	DWORD sentbytes = 0;

	if (self->tx_drop_ratio != 0 && rand() % 100 < self->tx_drop_ratio) {
		ARSDK_LOGI("transport_net %p: fd=%d tx drop %zu bytes",
				self, sock->fd, total);
		return total;
	}

	/* Destination address */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(*sock->txaddr);
	addr.sin_port = htons(*sock->txport);

	if (WSASendTo((SOCKET)sock->fd, wsabufs, wsabufcnt,
			&sentbytes, 0,
			(const struct sockaddr *)&addr, sizeof(addr),
			NULL, NULL) < 0) {
		return -errno;
	} else {
		return (ssize_t)sentbytes;
	}
}
#else /* !_WIN32 */
static ssize_t socket_write(struct arsdk_transport_net *self,
		struct socket *sock,
		struct iovec *iov, uint32_t iovcnt, size_t total)
{
	struct sockaddr_in addr;
	struct msghdr msg;
	ssize_t writelen = 0;

	if (self->tx_drop_ratio != 0 && rand() % 100 < self->tx_drop_ratio) {
		ARSDK_LOGI("transport_net %p: fd=%d tx drop %zu bytes",
				self, sock->fd, total);
		return total;
	}

	/* Destination address */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(*sock->txaddr);
	addr.sin_port = htons(*sock->txport);

	/* Construct socket message with address and iov */
	memset(&msg, 0, sizeof(msg));
	msg.msg_name = &addr;
	msg.msg_namelen = sizeof(addr);
	msg.msg_iov = iov;
	msg.msg_iovlen = iovcnt;

	/* Write ignoring interrupts */
	do {
		writelen = sendmsg(sock->fd, &msg, 0);
	} while (writelen < 0 && errno == EINTR);

	return writelen >= 0 ? writelen : -errno;
}
#endif /* !_WIN32 */

/**
 * Decodes protocol v1 header
 *
 * @param headerbuf : Data to read.
 * @param header[out] : Header to fill with data.
 * @param payload_len[out] : Payload length read from data.
 *
 * @return 0 in case of success, negative errno value in case of error.
 * @see ARSDK_PROTOCOL_VERSION_1
 */
static int decode_header_v1(const uint8_t *headerbuf,
		struct arsdk_transport_header *header, uint32_t *payload_len)
{
	uint32_t frame_len;

	/* Type less than ARSDK_TRANSPORT_DATA_TYPE_MAX */
	if (headerbuf[0] >= ARSDK_TRANSPORT_DATA_TYPE_MAX)
		return -EPROTO;

	header->type = headerbuf[0];
	header->id = headerbuf[1];
	/* Sequence number in 8 bits */
	header->seq = headerbuf[2];

	/* Frame size in 32 bits */
	frame_len = headerbuf[3] |
		      (headerbuf[4] << 8) |
		      (headerbuf[5] << 16) |
		      (headerbuf[6] << 24);
	if (frame_len < ARSDK_FRAME_V1_HEADER_SIZE)
		return -EPROTO;

	*payload_len = frame_len - ARSDK_FRAME_V1_HEADER_SIZE;
	return 0;
}

/**
 * Reads protocol version from data.
 *
 * @param src : Source where read.
 * @param src_len : Source length.
 * @param proto_v[out] : Protocol version read.
 * @param proto_v_len[out] : Length in byte read from the source.
 *
 * @return 0 in case of success, negative errno value in case of error.
 */
static int read_proto_v(const uint8_t *src, size_t src_len,
		uint32_t *proto_v, size_t *proto_v_len)
{
	int res = futils_varint_read_u32(src, src_len, proto_v, proto_v_len);
	if (res < 0)
		return res;

	/* If version is less than the offset, it is the protocol version 1
	   and there is no protocol version data. */
	if (*proto_v < ARSDK_TRANSPORT_DATA_TYPE_MAX) {
		*proto_v = ARSDK_PROTOCOL_VERSION_1;
		*proto_v_len = 0;
		return 0;
	}

	/* Subtract protocol version offset */
	*proto_v -= ARSDK_TRANSPORT_DATA_TYPE_MAX;
	return 0;
}

/**
 * Writes protocol version in data.
 *
 * @param dst : Destination where write.
 * @param dst_len : Destination length ; should be greater or equal to 5.
 * @param proto_v : Protocol version to write.
 *        Should be less than "UINT32_MAX - ARSDK_TRANSPORT_DATA_TYPE_MAX".
 * @param proto_v_len[out] : Length in byte written in the destination.
 *
 * @return 0 in case of success, negative errno value in case of error.
 */
static int write_proto_v(uint8_t *dst, size_t dst_len,
		uint32_t proto_v, size_t *proto_v_len)
{
	if (proto_v > UINT32_MAX - ARSDK_TRANSPORT_DATA_TYPE_MAX)
		return -EINVAL;

	/* Protocol version with offset */
	return futils_varint_write_u32(dst, dst_len,
			proto_v + ARSDK_TRANSPORT_DATA_TYPE_MAX,
			proto_v_len);
}

/**
 * Decodes protocol v2 header
 *
 * @param buf : Data to read.
 * @param len : Data size.
 * @param header[out] : Header to fill with data.
 * @param header_len[out] : Header length in the data buffer.
 * @param payload_len[out] : Payload length read from data.
 *
 * @return 0 in case of success, negative errno value in case of error.
 * @see ARSDK_PROTOCOL_VERSION_2
 */
static int decode_header_v2(const uint8_t *buf, size_t len,
		struct arsdk_transport_header *header, size_t *header_len,
		uint32_t *payload_len)
{
	int res;
	uint32_t proto_v = 0;
	const uint8_t *data = buf;
	size_t data_len = len;
	size_t val_len = 0;

	/* Type greater than ARSDK_TRANSPORT_DATA_TYPE_MAX */
	if (len == 0 || data[0] < ARSDK_TRANSPORT_DATA_TYPE_MAX)
		return -EPROTO;

	res = read_proto_v(data, data_len, &proto_v, &val_len);
	if (res < 0)
		return -EPROTO;
	data += val_len;
	data_len -= val_len;

	/* Subtract protocol version offset */
	if (proto_v < ARSDK_PROTOCOL_VERSION_2)
		return -EPROTO;

	/* Check if there is enough data to contain the minimum data to read. */
	if (data_len < 5)
		return -EPROTO;

	header->type = data[0];
	data++;
	data_len--;

	header->id = data[0];
	data++;
	data_len--;

	/* Sequence number in 16 bits */
	header->seq = data[0] |
		     (data[1] << 8);
	data += 2;
	data_len -= 2;

	res = futils_varint_read_u32(data, data_len, payload_len, &val_len);
	if (res < 0)
		return -EPROTO;
	data += val_len;
	data_len -= val_len;

	*header_len = len - data_len;
	return 0;
}

/**
 */
static void process_rxbuf(struct arsdk_transport_net *self,
		const uint8_t *rxbuf, uint32_t rxlen)
{
	int res = 0;
	uint32_t rxoff = 0, payloadlen = 0;
	struct arsdk_transport_header header;
	struct arsdk_transport_payload payload;
	const uint8_t *headerbuf, *payloadbuff;
	size_t header_size = self->cfg.proto_v > ARSDK_PROTOCOL_VERSION_1 ?
					ARSDK_FRAME_V2_HEADER_SIZE_MIN :
					ARSDK_FRAME_V1_HEADER_SIZE;
	while (rxoff < rxlen) {
		if (rxoff + header_size > rxlen) {
			ARSDK_LOGE("transport_net %p: partial header (%u)",
					self, (uint32_t)(rxlen - rxoff));
			return;
		}

		/* Decode header */
		memset(&header, 0, sizeof(header));
		headerbuf = &rxbuf[rxoff];
		if (self->cfg.proto_v == ARSDK_PROTOCOL_VERSION_1) {
			res = decode_header_v1(headerbuf, &header,
					&payloadlen);
			if (res < 0)
				goto error;
			rxoff += ARSDK_FRAME_V1_HEADER_SIZE;
		} else {
			res = decode_header_v2(headerbuf, rxlen - rxoff,
					&header, &header_size, &payloadlen);
			if (res < 0)
				goto error;
			rxoff += header_size;
		}

		/* Check header validity */
		if (rxoff + payloadlen > rxlen)
			goto error;

		/* Setup payload */
		payloadbuff = &rxbuf[rxoff];
		arsdk_transport_payload_init_with_data(&payload,
				(payloadlen == 0 ? NULL : payloadbuff),
				payloadlen);
		rxoff += payloadlen;

		/* Log received data */
		arsdk_transport_log_cmd(self->parent,
				headerbuf, header_size,
				&payload, ARSDK_CMD_DIR_RX);

		/* Process data */
		arsdk_transport_recv_data(self->parent, &header, &payload);
		arsdk_transport_payload_clear(&payload);
	}

	return;
error:
	ARSDK_LOGE("transport_net %p: bad frame", self);
	return;
}

/**
 */
static void data_fd_cb(int fd, uint32_t revents, void *userdata)
{
	struct arsdk_transport_net *self = userdata;
	ssize_t readlen = 0;

	/* Read data and check link status */
	readlen = socket_read(self, &self->data_sock, 1);
	if (readlen > 0)
		process_rxbuf(self, self->data_sock.rxbuf, (uint32_t)readlen);
}

/**
 */
static int arsdk_transport_net_dispose(struct arsdk_transport *base)
{
	struct arsdk_transport_net *self = arsdk_transport_get_child(base);
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	/* Free sockets */
	socket_cleanup(self, &self->data_sock);

	free(self);
	return 0;
}

/**
 */
static int arsdk_transport_net_start(struct arsdk_transport *base)
{
	int res = 0;
	struct arsdk_transport_net *self = arsdk_transport_get_child(base);
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	if (self->started)
		return -EBUSY;

	/* Start sockets */
	res = socket_start(self, &self->data_sock, &data_fd_cb);
	if (res < 0)
		goto error;

	self->started = 1;
	return 0;

	/* Cleanup in case of error */
error:
	socket_stop(self, &self->data_sock);
	return res;
}

/**
 */
static int arsdk_transport_net_stop(struct arsdk_transport *base)
{
	struct arsdk_transport_net *self = arsdk_transport_get_child(base);
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	if (!self->started)
		return 0;

	/* Stop sockets (ignore errors) */
	socket_stop(self, &self->data_sock);
	self->started = 0;

	return 0;
}

static void encode_header_v1(const struct arsdk_transport_header *header,
		uint32_t frame_size, uint8_t *headerbuf)
{
	headerbuf[0] = header->type;
	headerbuf[1] = header->id;
	/* Sequence number in 8 bits */
	headerbuf[2] = header->seq;
	/* Frame size number in 32 bits */
	headerbuf[3] = frame_size & 0xff;
	headerbuf[4] = (frame_size >> 8) & 0xff;
	headerbuf[5] = (frame_size >> 16) & 0xff;
	headerbuf[6] = (frame_size >> 24) & 0xff;
}

static int encode_header_v2(const struct arsdk_transport_header *header,
		uint32_t proto_v, uint32_t payload_len, uint8_t *buf,
		size_t buflen, size_t *headerlen)
{
	int res;
	uint8_t *data = buf;
	size_t data_len = buflen;
	size_t val_len = 0;

	if (proto_v > UINT32_MAX - ARSDK_TRANSPORT_DATA_TYPE_MAX)
		return -EINVAL;
	if (buflen < ARSDK_FRAME_V2_HEADER_SIZE_MAX)
		return -ENOBUFS;

	/* Protocol version */
	res = write_proto_v(data, data_len, proto_v, &val_len);
	if (res < 0)
		return res;
	data += val_len;
	data_len -= val_len;

	data[0] = header->type;
	data++;
	data_len--;

	data[0] = header->id;
	data++;
	data_len--;

	/* Sequence number in 16 bits */
	data[0] = header->seq & 0xff;
	data[1] = (header->seq >> 8) & 0xff;
	data += 2;
	data_len -= 2;

	/* Payload size */
	res = futils_varint_write_u32(data, data_len, payload_len, &val_len);
	if (res < 0)
		return res;
	data += val_len;
	data_len -= val_len;

	*headerlen = buflen - data_len;
	return 0;
}

/**
 */
static int arsdk_transport_net_send_data(struct arsdk_transport *base,
		const struct arsdk_transport_header *header,
		const struct arsdk_transport_payload *payload,
		const void *extra_hdr,
		size_t extra_hdrlen)
{
	int res = 0;
	struct arsdk_transport_net *self = arsdk_transport_get_child(base);
	uint8_t headerbuf[ARSDK_FRAME_V2_HEADER_SIZE_MAX];
	uint32_t size = 0;
	ssize_t writelen = 0;
	enum arsdk_link_status link_status = ARSDK_LINK_STATUS_KO;
	struct socket *sock = NULL;
	size_t header_size = 0;

#ifdef _WIN32
	WSABUF wsabufs[3];
	int wsabufcnt = 0;
#else /* !_WIN32 */
	struct iovec iov[3];
	int iovcnt = 0;
#endif /* !_WIN32 */

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(header != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(payload != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(extra_hdrlen == 0
			|| extra_hdr != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(payload->len == 0
			|| payload->cdata != NULL, -EINVAL);

	if (!self->started || self->data_sock.fd < 0)
		return -EPIPE;

	/* Determine socket to use */
	sock = &self->data_sock;

	/* Encode header */
	if (self->cfg.proto_v == ARSDK_PROTOCOL_VERSION_1) {
		header_size = ARSDK_FRAME_V1_HEADER_SIZE;
		size = header_size + extra_hdrlen + payload->len;
		encode_header_v1(header, size, headerbuf);
	} else {
		res = encode_header_v2(header, self->cfg.proto_v,
				extra_hdrlen + payload->len,
				headerbuf, sizeof(headerbuf), &header_size);
		if (res < 0)
			return res;
		size = header_size + extra_hdrlen + payload->len;
	}

	/* Log sent data (not for rtp/rtcp) */
	arsdk_transport_log_cmd(self->parent,
			headerbuf, header_size,
			payload, ARSDK_CMD_DIR_TX);

#ifdef _WIN32
	/* Setup wsabufs */
	wsabufs[wsabufcnt].buf = (char *)headerbuf;
	wsabufs[wsabufcnt++].len = header_size;
	if (extra_hdrlen > 0) {
		wsabufs[wsabufcnt].buf = (char *)extra_hdr;
		wsabufs[wsabufcnt++].len = extra_hdrlen;
	}
	if (payload->len > 0) {
		wsabufs[wsabufcnt].buf = (char *)payload->cdata;
		wsabufs[wsabufcnt++].len = payload->len;
	}

	/* Do the write */
	writelen = socket_write(self, sock, wsabufs, wsabufcnt, size);
#else /* !_WIN32 */
	/* Setup iov */
	iov[iovcnt].iov_base = headerbuf;
	iov[iovcnt++].iov_len = header_size;
	if (extra_hdrlen > 0) {
		iov[iovcnt].iov_base = (void *)extra_hdr;
		iov[iovcnt++].iov_len = extra_hdrlen;
	}
	if (payload->len > 0) {
		iov[iovcnt].iov_base = (void *)payload->cdata;
		iov[iovcnt++].iov_len = payload->len;
	}

	/* Do the write */
	writelen = socket_write(self, sock, iov, iovcnt, size);
#endif /* !_WIN32 */

	link_status = arsdk_transport_get_link_status(self->parent);
	if (writelen < 0) {
		res = writelen;
		/**
		 * On ios ENOBUFS error can be raised meaning
		 * the  output  queue for the network interface is full.
		 * we drop the packet and ignore error.
		 */
		if (res == -ENOBUFS) {
			ARSDK_LOGW("sendmsg(fd=%d, size=%u) err=%d(%s)",
				self->data_sock.fd, size, -res,
				strerror(-res));
			self->tx_fail++;
			res = 0;
		} else if (!ARSDK_WOULD_BLOCK(-res) &&
				link_status == ARSDK_LINK_STATUS_OK) {
			ARSDK_LOG_FD_ERRNO("sendmsg", self->data_sock.fd,
					-res);
			arsdk_transport_set_link_status(self->parent,
					ARSDK_LINK_STATUS_KO);
		}
	} else if ((uint32_t)writelen != size) {
		res = -EAGAIN;
		ARSDK_LOGE("Partial write on fd=%d (%u/%u)",
				self->data_sock.fd, (uint32_t)writelen, size);
	} else if (self->tx_fail > 0) {
		ARSDK_LOGI("sendmsg(fd=%d, size=%u) succeed after %d failures",
			self->data_sock.fd, size, self->tx_fail);
		self->tx_fail = 0;
	}

	return res;
}

static uint32_t arsdk_transport_net_get_proto_v(struct arsdk_transport *base)
{
	struct arsdk_transport_net *self = arsdk_transport_get_child(base);
	ARSDK_RETURN_VAL_IF_FAILED(self != NULL, -EINVAL, 0);
	return self->cfg.proto_v;
}

/** */
static const struct arsdk_transport_ops s_arsdk_transport_net_ops = {
	.dispose = &arsdk_transport_net_dispose,
	.start = &arsdk_transport_net_start,
	.stop = &arsdk_transport_net_stop,
	.send_data = &arsdk_transport_net_send_data,
	.get_proto_v = &arsdk_transport_net_get_proto_v,
};

/**
 */
int arsdk_transport_net_new(struct pomp_loop *loop,
		const struct arsdk_transport_net_cfg *cfg,
		const struct arsdk_transport_net_cbs *cbs,
		struct arsdk_transport_net **ret_obj)
{
	int res = 0;
	struct arsdk_transport_net *self = NULL;
	char *val = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(loop != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cbs->socketcb != NULL, -EINVAL);

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Initialize structure (make sure socket fds are setup before handling
	 * errors) */
	self->loop = loop;
	self->cfg = *cfg;
	self->cbs = *cbs;
	self->data_sock.fd = -1;

	/* For debug/test get rx/tx drop ration (percentage) from environment */
	val = getenv("ARSDK_TRANSPORT_NET_RX_DROP_RATIO");
	if (val != NULL)
		self->rx_drop_ratio = atoi(val);
	val = getenv("ARSDK_TRANSPORT_NET_TX_DROP_RATIO");
	if (val != NULL)
		self->tx_drop_ratio = atoi(val);

	/* Setup base structure */
	res = arsdk_transport_new(self, &s_arsdk_transport_net_ops, loop,
			ARSDK_TRANSPORT_PING_PERIOD, ARSDK_TRANSPORT_TAG,
			&self->parent);
	if (res < 0)
		goto error;

	/* Data socket */
	self->data_sock.txaddr = &self->cfg.tx_addr;
	self->data_sock.rxport = &self->cfg.data.rx_port;
	self->data_sock.txport = &self->cfg.data.tx_port;
	self->data_sock.rxenabled = 1;
	self->data_sock.txenabled = 1;
	res = socket_setup(self, &self->data_sock, ARSDK_SOCKET_KIND_COMMAND);
	if (res < 0)
		goto error;

	/* Success */
	*ret_obj = self;
	return 0;

	/* Cleanup in case of error */
error:
	arsdk_transport_destroy(self->parent);
	return res;
}

/**
 */
struct arsdk_transport *arsdk_transport_net_get_parent(
		struct arsdk_transport_net *self)
{
	return self == NULL ? NULL : self->parent;
}

/**
 */
int arsdk_transport_net_get_cfg(struct arsdk_transport_net *self,
		struct arsdk_transport_net_cfg *cfg)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg != NULL, -EINVAL);
	*cfg = self->cfg;
	return 0;
}

/**
 */
int arsdk_transport_net_update_cfg(struct arsdk_transport_net *self,
		const struct arsdk_transport_net_cfg *cfg)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg != NULL, -EINVAL);
	/* TODO: check that only tx fields are changed */
	self->cfg = *cfg;
	return 0;
}

/**
 */
int arsdk_transport_net_socket_cb(struct arsdk_transport_net *self,
			int fd,
			enum arsdk_socket_kind kind)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	(*self->cbs.socketcb)(self, fd, kind, self->cbs.userdata);
	return 0;
}
