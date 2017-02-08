/*
  ISC License

  Copyright (c) 2016-2017, Jo-Philipp Wich <jo@mein.io>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
  REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
  INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
  LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
  OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
  PERFORMANCE OF THIS SOFTWARE.
*/

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <libubox/uloop.h>
#include <libubox/usock.h>

#include "socket.h"
#include "database.h"
#include "timing.h"
#include "nlbwmon.h"

struct command {
	const char *cmd;
	int (*cb)(int sock);
};

static int ctrl_socket;
static struct uloop_fd sock_fd = { };
static struct uloop_timeout sock_tm = { };


static int
handle_dump(int sock)
{
	struct record *rec = NULL;

	if (send(sock, gdbh->db, sizeof(*gdbh->db), 0) != sizeof(*gdbh->db))
		return -errno;

	while ((rec = database_next(gdbh, rec)) != NULL)
		if (send(sock, rec, db_recsize, 0) != db_recsize)
			return -errno;

	return 0;
}

static int
handle_list(int sock)
{
	int err;
	int delta = 0;
	uint32_t timestamp;

	while (true) {
		timestamp = interval_timestamp(&opt.archive_interval, delta--);
		err = database_load(NULL, opt.db.directory, timestamp);

		if (err)
			break;

		if (send(sock, &timestamp, sizeof(timestamp), 0) != sizeof(timestamp))
			return -errno;
	}

	return 0;
}

static int
handle_commit(int sock)
{
	uint32_t timestamp = interval_timestamp(&opt.archive_interval, 0);
	char buf[128];
	int err, len;

	err = database_save(gdbh, opt.db.directory, timestamp, opt.db.compress);
	len = snprintf(buf, sizeof(buf), "%d %s", -err,
	               err ? strerror(-err) : "ok");

	if (send(sock, buf, len, 0) != len)
		return -errno;

	return 0;
}

static struct command commands[] = {
	{ "dump", handle_dump },
	{ "list", handle_list },
	{ "commit", handle_commit },
};


static void
handle_client_accept(struct uloop_fd *ufd, unsigned int ev);

static void
handle_client_timeout(struct uloop_timeout *tm)
{
	uloop_timeout_cancel(&sock_tm);

	uloop_fd_delete(&sock_fd);
	close(sock_fd.fd);

	sock_fd.cb = handle_client_accept;
	sock_fd.fd = ctrl_socket;

	uloop_fd_add(&sock_fd, ULOOP_READ);
}

static void
handle_client_request(struct uloop_fd *ufd, unsigned int ev)
{
	char cmd[32] = { };
	size_t len;
	int i, err;

	len = recv(ufd->fd, cmd, sizeof(cmd) - 1, 0);

	if (len > 0)
		for (i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
			if (!strcmp(commands[i].cmd, cmd)) {
				err = commands[i].cb(ufd->fd);
				if (err) {
					fprintf(stderr, "Unable to handle '%s' command: %s\n",
					        cmd, strerror(-err));
				}
			}

	handle_client_timeout(&sock_tm);
}

static void
handle_client_accept(struct uloop_fd *ufd, unsigned int ev)
{
	int fd;

	fd = accept(ufd->fd, NULL, NULL);

	if (fd < 0)
		return;

	uloop_fd_delete(ufd);

	ufd->cb = handle_client_request;
	ufd->fd = fd;

	uloop_fd_add(ufd, ULOOP_READ);

	sock_tm.cb = handle_client_timeout;
	uloop_timeout_set(&sock_tm, 100);
}


int
socket_init(const char *path)
{
	struct stat s;

	if (!stat(path, &s) && S_ISSOCK(s.st_mode)) {
		if (unlink(path))
			return -errno;
	}

	ctrl_socket = usock(USOCK_UNIX|USOCK_SERVER, path, NULL);

	if (ctrl_socket < 0)
		return -errno;

	sock_fd.fd = ctrl_socket;
	sock_fd.cb = handle_client_accept;

	uloop_fd_add(&sock_fd, ULOOP_READ);

	return 0;
}
