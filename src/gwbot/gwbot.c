// SPDX-License-Identifier: GPL-2.0-only
/*
 *  src/gwbot/gwbot.c
 *
 *  Core GNUWeebBot
 *
 *  Copyright (C) 2021  Ammar Faizi
 */

#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <gwbot/common.h>
#include <gwbot/lib/tg_api/send_message.h>


struct gwbot_state {
	bool			stop_event_loop;

	/* Interrupt signal */
	int			intr_sig;

	int			tcp_fd;
	int			epoll_fd;
	struct gwbot_cfg	*cfg;
};


static struct gwbot_state *g_state;


static void handle_interrupt(int sig)
{
	struct gwbot_state *state = g_state;
	state->intr_sig = sig;
	state->stop_event_loop = true;
	putchar('\n');
}


static int validate_cfg(struct gwbot_cfg *cfg)
{
	struct gwbot_sock_cfg *sock = &cfg->sock;
	struct gwbot_cred_cfg *cred = &cfg->cred;

	if (sock->bind_addr == NULL || *sock->bind_addr == '\0') {
		pr_err("sock->bind_addr cannot be empty!");
		return -1;
	}

	if (sock->bind_port == 0) {
		pr_err("sock->bind_port cannot be empty");
		return -1;
	}

	if (cred->token == NULL || *cred->token == '\0') {
		pr_err("cred->token cannot be empty");
		return -1;
	}

	return 0;
}


static int init_state(struct gwbot_state *state)
{
	state->stop_event_loop = false;
	state->intr_sig        = 0;
	state->tcp_fd          = -1;
	state->epoll_fd        = -1;
	return 0;
}


static int socket_setup(int tcp_fd, struct gwbot_state *state)
{
int y;
	int err;
	int retval;
	const char *lv, *on; /* level and optname */
	socklen_t len = sizeof(y);
	struct gwbot_cfg *cfg = state->cfg;
	const void *py = (const void *)&y;

	y = 1;
	retval = setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR, py, len);
	if (unlikely(retval < 0)) {
		lv = "SOL_SOCKET";
		on = "SO_REUSEADDR";
		goto out_err;
	}

	y = 1;
	retval = setsockopt(tcp_fd, SOL_SOCKET, SO_REUSEADDR, py, len);
	if (unlikely(retval < 0)) {
		lv = "SOL_SOCKET";
		on = "SO_REUSEADDR";
		goto out_err;
	}

	y = 1;
	retval = setsockopt(tcp_fd, IPPROTO_TCP, TCP_NODELAY, py, len);
	if (unlikely(retval < 0)) {
		lv = "IPPROTO_TCP";
		on = "TCP_NODELAY";
		goto out_err;
	}

	/*
	 * Use cfg to set some socket options.
	 */
	(void)cfg;
	return retval;

out_err:
	err = errno;
	pr_err("setsockopt(tcp_fd, %s, %s): " PRERF, lv, on, PREAR(err));
	return retval;
}


static int init_socket(struct gwbot_state *state)
{
	int err;
	int tcp_fd;
	int ret = 0;
	struct sockaddr_in addr;
	socklen_t addr_len = sizeof(addr);

	tcp_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
	if (unlikely(tcp_fd < 0)) {
		err = errno;
		pr_err("socket(): " PRERF, PREAR(err));
		return -1;
	}

	ret = socket_setup(tcp_fd, state);
	if (unlikely(ret < 0)) {
		ret = -1;
		goto out;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(state->cfg->sock.bind_port);
	addr.sin_addr.s_addr = inet_addr(state->cfg->sock.bind_addr);

	ret = bind(tcp_fd, (struct sockaddr *)&addr, addr_len);
	if (unlikely(ret < 0)) {
		ret = -1;
		err = errno;
		pr_err("bind(): " PRERF, PREAR(err));
		goto out;
	}

	ret = listen(tcp_fd, state->cfg->sock.backlog);
	if (unlikely(ret < 0)) {
		ret = -1;
		err = errno;
		pr_err("listen(): " PRERF, PREAR(err));
		goto out;
	}

	pr_notice("Listening on %s:%d...", state->cfg->sock.bind_addr,
		  state->cfg->sock.bind_port);

	state->tcp_fd = tcp_fd;
out:
	if (unlikely(ret != 0))
		close(tcp_fd);
	return ret;
}


static void close_file_descriptors(struct gwbot_state *state)
{
	int tcp_fd = state->tcp_fd;

	if (likely(tcp_fd != -1)) {
		pr_notice("Closing state->tcp_fd (%d)", tcp_fd);
		close(tcp_fd);
	}
}


static void destroy_state(struct gwbot_state *state)
{
	close_file_descriptors(state);
}


int gwbot_run(struct gwbot_cfg *cfg)
{
	int ret;
	struct gwbot_state state;

	/* Shut the valgrind up! */
	memset(&state, 0, sizeof(state));

	state.cfg = cfg;
	g_state = &state;
	signal(SIGHUP, handle_interrupt);
	signal(SIGINT, handle_interrupt);
	signal(SIGTERM, handle_interrupt);
	signal(SIGQUIT, handle_interrupt);
	signal(SIGPIPE, SIG_IGN);

	tg_api_global_init();

	ret = validate_cfg(cfg);
	if (unlikely(ret < 0))
		goto out;
	ret = init_state(&state);
	if (unlikely(ret < 0))
		goto out;
	ret = init_socket(&state);
	if (unlikely(ret < 0))
		goto out;
out:
	tg_api_global_destroy();
	destroy_state(&state);
	return ret;

	// /* Minimal working example */
	// tg_api_handle *handle;

	// tg_api_smsg msg = {
	// 	.chat_id = -1001422514298, /* GNU/Weeb TDD group */
	// 	.reply_to_msg_id = 312, /* 0 means don't reply to msg */
	// 	.parse_mode = PARSE_MODE_HTML,
	// 	.text = "AAAAA\n<b>Test reply to message</b>"
	// };


	// handle = tg_api_hcreate(cfg->token);
	// tga_send_msg(handle, &msg);

	// printf("%s\n", tg_api_res_get_body(&handle->res));


	// /*
	//  * Must destroy the handle if it is not used anymore.
	//  */
	// tg_api_destroy(handle);

	return 0;
}
