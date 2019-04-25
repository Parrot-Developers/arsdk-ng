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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <libpomp.h>
#include <libmux.h>

#include "arsdk/arsdk.h"
#define ULOG_TAG device
#include "ulog.h"
ULOG_DECLARE_TAG(device);

#define LOGD(_fmt, ...)	ULOGD(_fmt, ##__VA_ARGS__)
#define LOGI(_fmt, ...)	ULOGI(_fmt, ##__VA_ARGS__)
#define LOGW(_fmt, ...)	ULOGW(_fmt, ##__VA_ARGS__)
#define LOGE(_fmt, ...)	ULOGE(_fmt, ##__VA_ARGS__)

/** Log error with errno */
#define LOG_ERRNO(_fct, _err) \
	LOGE("%s:%d: %s err=%d(%s)", __func__, __LINE__, \
			_fct, _err, strerror(_err))

/** Log error with fd and errno */
#define LOG_FD_ERRNO(_fct, _fd, _err) \
	LOGE("%s:%d: %s(fd=%d) err=%d(%s)", __func__, __LINE__, \
			_fct, _fd, _err, strerror(_err))

/** */
struct app {
	int                          stopped;
	enum arsdk_backend_type      backend_type;
	struct pomp_loop             *loop;
	struct arsdk_mngr            *mngr;
	struct arsdk_backend_mux     *backend_mux;
	struct arsdk_publisher_mux   *publisher_mux;
	struct arsdk_backend_net     *backend_net;
	struct arsdk_publisher_avahi *publisher_avahi;
	struct arsdk_publisher_net   *publisher_net;
	int                          use_publisher_avahi;
	int                          use_publisher_net;
	struct arsdk_peer            *peer;
	struct arsdk_cmd_itf         *cmd_itf;

	struct {
		struct mux_ctx    *ctx;
		int               sockfd_server;
		int               sockfd;
	} mux;

	struct {
		const char        *filepath;
		FILE              *file;
		struct pomp_timer *timer;
	} replay;
};

/** */
static struct app s_app = {
	.stopped = 0,
	.backend_type = ARSDK_BACKEND_TYPE_UNKNOWN,
	.loop = NULL,
	.backend_net = NULL,
	.backend_mux = NULL,
	.publisher_mux = NULL,
	.publisher_avahi = NULL,
	.publisher_net = NULL,
	.use_publisher_avahi = 1,
	.use_publisher_net = 0,
	.peer = NULL,
	.cmd_itf = NULL,
	.mux = {
		.ctx = NULL,
		.sockfd_server = -1,
		.sockfd = -1,
	},
	.replay = {
		.filepath = NULL,
		.file = NULL,
		.timer = NULL,
	},
};

/**
 */
static void send_status(struct arsdk_cmd_itf *itf,
		const struct arsdk_cmd *cmd,
		enum arsdk_cmd_itf_send_status status,
		int done,
		void *userdata)
{
	LOGI("cmd %u,%u,%u: %s%s", cmd->prj_id, cmd->cls_id, cmd->cmd_id,
			arsdk_cmd_itf_send_status_str(status),
			done ? "(DONE)" : "");
}

/**
 */
static void link_quality(struct arsdk_cmd_itf *itf,
		int32_t tx_quality,
		int32_t rx_quality,
		int32_t rx_useful,
		void *userdata)
{
	LOGI("link_quality tx_quality:%d%% rx_quality:%d%% rx_useful:%d%%",
			tx_quality,
			rx_quality,
			rx_useful);

}

/**
 */
static void recv_cmd(struct arsdk_cmd_itf *itf,
		const struct arsdk_cmd *cmd,
		void *userdata)
{
	int res = 0;
	char buf[512] = "";
	struct app *app = userdata;
	struct arsdk_cmd cmd2;
	LOGI("%s", __func__);

	/* Format and log received command */
	arsdk_cmd_fmt(cmd, buf, sizeof(buf));
	LOGI("%s", buf);

	/* Create 'GPSFixStateChanged' command' */
	arsdk_cmd_init(&cmd2);
	res = arsdk_cmd_enc_Ardrone3_GPSSettingsState_GPSFixStateChanged(&cmd2,
			0/*_fixed)*/);
	if (res < 0)
		LOG_ERRNO("arsdk_cmd_enc", -res);

	/* Send command */
	res = arsdk_cmd_itf_send(app->cmd_itf, &cmd2, &send_status, app);
	if (res < 0)
		LOG_ERRNO("arsdk_cmd_itf_send", -res);

	arsdk_cmd_clear(&cmd2);

	switch (cmd->id) {
	case ARSDK_ID_COMMON_SETTINGS_ALLSETTINGS:
		res = arsdk_cmd_send_Common_SettingsState_AllSettingsChanged(
				app->cmd_itf, &send_status, app);
		break;
	case ARSDK_ID_COMMON_COMMON_ALLSTATES:
		res = arsdk_cmd_send_Common_CommonState_AllStatesChanged(
				app->cmd_itf, &send_status, app);
		break;
	}
}

/**
 */
static void connected(struct arsdk_peer *peer,
		const struct arsdk_peer_info *info,
		void *userdata)
{
	int res = 0;
	struct app *app = userdata;
	struct arsdk_cmd_itf_cbs cmd_cbs;
	LOGI("%s", __func__);

	/* Create command interface object */
	memset(&cmd_cbs, 0, sizeof(cmd_cbs));
	cmd_cbs.userdata = app;
	cmd_cbs.recv_cmd = &recv_cmd;
	cmd_cbs.send_status = &send_status;
	cmd_cbs.link_quality = &link_quality;
	res = arsdk_peer_create_cmd_itf(peer, &cmd_cbs, &app->cmd_itf);
	if (res < 0)
		LOG_ERRNO("arsdk_peer_create_cmd_itf", -res);
}

/**
 */
static void disconnected(struct arsdk_peer *peer,
		const struct arsdk_peer_info *info,
		void *userdata)
{
	struct app *app = userdata;
	LOGI("%s", __func__);

	app->cmd_itf = NULL;
	app->peer = NULL;
}

/**
 */
static void canceled(struct arsdk_peer *peer,
		const struct arsdk_peer_info *info,
		enum arsdk_conn_cancel_reason reason,
		void *userdata)
{
	struct app *app = userdata;
	LOGI("%s: reason=%s", __func__, arsdk_conn_cancel_reason_str(reason));
	app->peer = NULL;
}

/**
 */
static void link_status(struct arsdk_peer *peer,
		const struct arsdk_peer_info *info,
		enum arsdk_link_status status,
		void *userdata)
{
	LOGI("%s: status=%s", __func__, arsdk_link_status_str(status));
	if (status == ARSDK_LINK_STATUS_KO)
		arsdk_peer_disconnect(peer);
}

/**
 */
static void conn_req(struct arsdk_peer *peer,
		const struct arsdk_peer_info *info,
		void *userdata)
{
	int res = 0;
	struct app *app = userdata;
	struct arsdk_peer_conn_cfg cfg;
	struct arsdk_peer_conn_cbs cbs;
	static const char json[] = "{"
		"\"arstream_fragment_size\": 65000, "
		"\"arstream_fragment_maximum_number\": 4, "
		"\"c2d_update_port\": 51 ,"
		"\"c2d_user_port\": 21"
		"}";
	LOGI("%s", __func__);

	/* Only one peer at a time */
	if (app->peer != NULL) {
		res = arsdk_peer_reject(peer);
		if (res < 0)
			LOG_ERRNO("arsdk_peer_reject", -res);
		return;
	}

	/* Save peer */
	app->peer = peer;

	memset(&cfg, 0, sizeof(cfg));
	cfg.json = json;

	memset(&cbs, 0, sizeof(cbs));
	cbs.userdata = app;
	cbs.connected = &connected;
	cbs.disconnected = &disconnected;
	cbs.canceled = &canceled;
	cbs.link_status = &link_status;

	/* Accept connection */
	res = arsdk_peer_accept(peer, &cfg, &cbs, app->loop);
	if (res < 0)
		LOG_ERRNO("arsdk_peer_accept", -res);
}

/**
 */
static void socket_cb(struct arsdk_backend_net *self, int fd,
		enum arsdk_socket_kind kind, void *userdata)
{
	LOGI("socket_cb : self:%p fd:%d kind:%s userdata:%p",
			self, fd, arsdk_socket_kind_str(kind), userdata);
}

/**
 */
static void backend_create(struct app *app)
{
	int res = 0;
	struct arsdk_backend_net_cfg backend_net_cfg;
	struct arsdk_backend_mux_cfg backend_mux_cfg;
	struct arsdk_publisher_avahi_cfg publisher_avahi_cfg;
	struct arsdk_publisher_net_cfg publisher_net_cfg;
	struct arsdk_publisher_cfg publisher_cfg = {
		.name = "ARDrone service",
		.type = ARSDK_DEVICE_TYPE_BEBOP_2,
		.id = "12345678",
	};
	struct arsdk_backend_listen_cbs listen_cbs = {
		.userdata = app,
		.conn_req = &conn_req,
	};
	uint16_t net_listen_port = 44444;

	switch (app->backend_type) {
	case ARSDK_BACKEND_TYPE_NET:
		memset(&backend_net_cfg, 0, sizeof(backend_net_cfg));
		res = arsdk_backend_net_new(app->mngr, &backend_net_cfg,
				&app->backend_net);
		if (res < 0)
			LOG_ERRNO("arsdk_backend_net_new", -res);

		res = arsdk_backend_net_set_socket_cb(app->backend_net,
				&socket_cb, &s_app);
		if (res < 0)
			LOG_ERRNO("arsdk_backend_net_set_socket_cb", -res);

		res = arsdk_backend_net_start_listen(app->backend_net,
				&listen_cbs, net_listen_port);
		if (res < 0)
			LOG_ERRNO("arsdk_backend_net_start_listen", -res);

		if (app->use_publisher_avahi) {
			/* start avahi publisher */
			memset(&publisher_avahi_cfg, 0,
					sizeof(publisher_avahi_cfg));
			publisher_avahi_cfg.base = publisher_cfg;
			publisher_avahi_cfg.port = net_listen_port;

			res = arsdk_publisher_avahi_new(app->backend_net,
					app->loop, &app->publisher_avahi);
			if (res < 0)
				LOG_ERRNO("arsdk_publisher_avahi_new", -res);

			res = arsdk_publisher_avahi_start(app->publisher_avahi,
					&publisher_avahi_cfg);
			if (res < 0)
				LOG_ERRNO("arsdk_publisher_avahi_start", -res);

		}

		if (app->use_publisher_net) {
			/* start net publisher */
			memset(&publisher_net_cfg, 0,
					sizeof(publisher_net_cfg));
			publisher_net_cfg.base = publisher_cfg;
			publisher_net_cfg.port = net_listen_port;

			res = arsdk_publisher_net_new(app->backend_net,
					app->loop, NULL, &app->publisher_net);
			if (res < 0)
				LOG_ERRNO("arsdk_publisher_net_new", -res);

			res = arsdk_publisher_net_start(app->publisher_net,
					&publisher_net_cfg);
			if (res < 0)
				LOG_ERRNO("arsdk_publisher_net_start", -res);
		}
		break;
	case ARSDK_BACKEND_TYPE_MUX:
		memset(&backend_mux_cfg, 0, sizeof(backend_mux_cfg));
		backend_mux_cfg.mux = app->mux.ctx;
		res = arsdk_backend_mux_new(app->mngr, &backend_mux_cfg,
				&app->backend_mux);
		if (res < 0)
			LOG_ERRNO("arsdk_backend_mux_new", -res);
		res = arsdk_backend_mux_start_listen(app->backend_mux,
				&listen_cbs);
		if (res < 0)
			LOG_ERRNO("arsdk_backend_mux_start_listen", -res);

		/* start mux publisher */
		res = arsdk_publisher_mux_new(app->backend_mux, app->mux.ctx,
				&app->publisher_mux);
		if (res < 0)
			LOG_ERRNO("arsdk_publisher_mux_new", -res);

		res = arsdk_publisher_mux_start(app->publisher_mux,
				&publisher_cfg);
		if (res < 0)
			LOG_ERRNO("arsdk_publisher_mux_start", -res);
		break;

	case ARSDK_BACKEND_TYPE_BLE: /* NO BREAK */
	case ARSDK_BACKEND_TYPE_UNKNOWN: /* NO BREAK */
	default:
		LOGW("Unsupported backend: %s",
				arsdk_backend_type_str(app->backend_type));
		return;
	}

	/* wait for connection request */
}

/**
 */
static void backend_destroy(struct app *app)
{
	int res = 0;

	if (app->publisher_avahi) {
		res = arsdk_publisher_avahi_stop(app->publisher_avahi);
		if (res < 0)
			LOG_ERRNO("arsdk_publisher_avahi_stop", -res);

		arsdk_publisher_avahi_destroy(app->publisher_avahi);
		app->publisher_avahi = NULL;
	}

	if (app->publisher_net) {
		res = arsdk_publisher_net_stop(app->publisher_net);
		if (res < 0)
			LOG_ERRNO("arsdk_publisher_net_stop", -res);

		arsdk_publisher_net_destroy(app->publisher_net);
		app->publisher_net = NULL;
	}

	if (app->publisher_mux) {
		res = arsdk_publisher_mux_stop(app->publisher_mux);
		if (res < 0)
			LOG_ERRNO("arsdk_publisher_mux_stop", -res);

		arsdk_publisher_mux_destroy(app->publisher_mux);
		app->publisher_mux = NULL;
	}

	if (app->peer != NULL) {
		res = arsdk_peer_disconnect(app->peer);
		if (res < 0)
			LOG_ERRNO("arsdk_peer_disconnect", -res);
		if (app->peer != NULL)
			LOGE("s_app.peer should be NULL");
		app->peer = NULL;
	}

	if (app->backend_mux != NULL) {
		res = arsdk_backend_mux_destroy(app->backend_mux);
		if (res < 0)
			LOG_ERRNO("arsdk_backend_mux_destroy", -res);

		app->backend_mux = NULL;
	}

	if (app->backend_net != NULL) {
		res = arsdk_backend_net_stop_listen(app->backend_net);
		if (res < 0)
			LOG_ERRNO("arsdk_backend_net_stop_listen", -res);

		res = arsdk_backend_net_destroy(app->backend_net);
		if (res < 0)
			LOG_ERRNO("arsdk_backend_net_destroy", -res);

		app->backend_net = NULL;
	}
}

/**
 */
static void mux_server_fdeof(struct mux_ctx *ctx, void *userdata)
{
	struct app *app = userdata;
	mux_stop(app->mux.ctx);
	backend_destroy(app);
	mux_unref(app->mux.ctx);
	app->mux.ctx = NULL;
	close(app->mux.sockfd);
	app->mux.sockfd = -1;
}

/**
 */
static void mux_server_fd_event_cb(int fd, uint32_t revents, void *userdata)
{
	struct app *app = userdata;
	int sockfd = -1;
	struct mux_ops ops;

	sockfd = accept(app->mux.sockfd_server, NULL, NULL);
	if (sockfd < 0)
		LOG_FD_ERRNO("accept", app->mux.sockfd_server, errno);

	if (app->mux.sockfd != -1) {
		close(sockfd);
		return;
	}

	/* For testing purposes, set the not pollable flag for fd */
	memset(&ops, 0, sizeof(ops));
	ops.userdata = app;
	ops.fdeof = mux_server_fdeof;
	app->mux.sockfd = sockfd;
	app->mux.ctx = mux_new(app->mux.sockfd, app->loop, &ops,
			MUX_FLAG_FD_NOT_POLLABLE);

	backend_create(app);
}

/**
 */
static void mux_server_create(struct app *app)
{
	int res = 0;
	struct sockaddr_in addr;
	socklen_t addrlen = 0;
	int sockopt = 0;

	app->mux.sockfd_server = socket(AF_INET, SOCK_STREAM |
			SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (app->mux.sockfd_server < 0)
		LOG_ERRNO("socket", errno);

	/* Allow reuse of address */
	sockopt = 1;
	if (setsockopt(app->mux.sockfd_server, SOL_SOCKET, SO_REUSEADDR,
			&sockopt, sizeof(sockopt)) < 0) {
		LOG_FD_ERRNO("setsockopt.SO_REUSEADDR", app->mux.sockfd_server,
				errno);
	}

	/* Setup address */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(4321);
	addrlen = sizeof(addr);

	if (bind(app->mux.sockfd_server,
			(const struct sockaddr *)&addr, addrlen) < 0) {
		LOG_FD_ERRNO("bind", app->mux.sockfd_server, errno);
	}

	if (listen(app->mux.sockfd_server, SOMAXCONN) < 0)
		LOG_FD_ERRNO("listen", app->mux.sockfd_server, errno);

	res = pomp_loop_add(app->loop, app->mux.sockfd_server,
			POMP_FD_EVENT_IN, &mux_server_fd_event_cb, app);
	if (res < 0)
		LOG_ERRNO("pomp_loop_add", -res);
}

/**
 */
static void mux_server_destroy(struct app *app)
{
	if (app->mux.ctx != NULL) {
		shutdown(app->mux.sockfd, SHUT_RDWR);
		mux_server_fdeof(app->mux.ctx, app);
	}

	if (app->mux.sockfd_server >= 0) {
		pomp_loop_remove(app->loop, app->mux.sockfd_server);
		close(app->mux.sockfd_server);
		app->mux.sockfd_server = -1;
	}
}

/**
 */
static void sig_handler(int signum)
{
	LOGI("signal %d(%s) received", signum, strsignal(signum));
	s_app.stopped = 1;
	if (s_app.loop != NULL)
		pomp_loop_wakeup(s_app.loop);
}

/**
 */
static void usage(const char *progname)
{
	fprintf(stderr, "usage: %s [<options>]\n", progname);
	fprintf(stderr, "  -h --help   : print this help message and exit\n"
		"  --publisher-avahi\n"
		"  --no-publisher-avahi\n"
		"  --publisher-net\n"
		"  --no-publisher-net\n"
		"  --mux\n"
		"  --codec <codec>\n"
		"  --replay-file <file>\n");
}

/**
 */
int main(int argc, char *argv[])
{
	int res = 0;
	int argidx = 0;

	/* Setup signal handlers */
	signal(SIGINT, &sig_handler);
	signal(SIGTERM, &sig_handler);
	signal(SIGPIPE, SIG_IGN);

	s_app.backend_type = ARSDK_BACKEND_TYPE_NET;
	for (argidx = 1; argidx < argc; argidx++) {
		if (argv[argidx][0] != '-') {
			/* End of options */
			break;
		} else if (strcmp(argv[argidx], "-h") == 0
				|| strcmp(argv[argidx], "--help") == 0) {
			/* Help */
			usage(argv[0]);
			return 0;
		} else if (strcmp(argv[argidx], "--replay-file") == 0) {
			if (argidx + 1 >= argc) {
				fprintf(stderr, "Missing replay file\n");
				usage(argv[0]);
				return -1;
			}
			s_app.replay.filepath = argv[++argidx];
		} else if (strcmp(argv[argidx], "--publisher-avahi") == 0) {
			s_app.backend_type = ARSDK_BACKEND_TYPE_NET;
			s_app.use_publisher_avahi = 1;
		} else if (strcmp(argv[argidx], "--no-publisher-avahi") == 0) {
			s_app.use_publisher_avahi = 0;
		} else if (strcmp(argv[argidx], "--mux") == 0) {
			s_app.backend_type = ARSDK_BACKEND_TYPE_MUX;
		} else if (strcmp(argv[argidx], "--publisher-net") == 0) {
			s_app.backend_type = ARSDK_BACKEND_TYPE_NET;
			s_app.use_publisher_net = 1;
		} else if (strcmp(argv[argidx], "--no-publisher-net") == 0) {
			s_app.use_publisher_net = 0;
		}
	}

	/* Create loop */
	s_app.loop = pomp_loop_new();

	/* Create manager */
	res = arsdk_mngr_new(s_app.loop, &s_app.mngr);
	if (res < 0) {
		LOG_ERRNO("arsdk_mngr_new", -res);
		goto out;
	}

	/* Create backend */
	if (s_app.backend_type == ARSDK_BACKEND_TYPE_MUX)
		mux_server_create(&s_app);
	else
		backend_create(&s_app);

	/* Run loop */
	while (!s_app.stopped)
		pomp_loop_wait_and_process(s_app.loop, -1);

	/* Cleanup */
out:
	if (s_app.backend_type == ARSDK_BACKEND_TYPE_MUX)
		mux_server_destroy(&s_app);

	backend_destroy(&s_app);

	if (s_app.loop != NULL) {
		res = pomp_loop_destroy(s_app.loop);
		if (res < 0)
			LOG_ERRNO("pomp_loop_destroy", -res);
		s_app.loop = NULL;
	}

	return 0;
}
