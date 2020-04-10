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


#include <libubox/avl.h>
#include <libubox/list.h>
#include <libubox/uloop.h>
#include <libubox/usock.h>
#include <libubox/utils.h>

#include <endian.h>
#include <arpa/inet.h>
#include <netinet/ether.h>

#include <netdb.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>

#include <endian.h>

#include "nlbwmon.h"
#include "neigh.h"
#include "database.h"
#include "protocol.h"
#include "nfnetlink.h"
#include "subnets.h"
#include "socket.h"
#include "client.h"
#include "utils.h"

static struct uloop_timeout commit_tm = { };
static struct uloop_timeout refresh_tm = { };

struct options opt = {
	.commit_interval = 86400,
	.refresh_interval = 30,

	.netlink_buffer_size = 524288,

	.tempdir = "/tmp",
	.socket = "/var/run/nlbwmon.sock",
	.protocol_db = "/usr/share/nlbwmon/protocols",

	.db = {
		.directory = "/usr/share/nlbwmon/db"
	}
};


static void save_persistent(uint32_t timestamp)
{
	int err;

	err = database_save(gdbh, opt.db.directory, timestamp, opt.db.compress);

	if (err == -EEXIST) {
		fprintf(stderr, "Existing database found, merging values\n");

		err = database_load(gdbh, opt.db.directory, timestamp);

		if (err) {
			fprintf(stderr, "Unable to load existing database: %s\n",
			        strerror(-err));
		}
	}

	err = database_save(gdbh, opt.db.directory, timestamp, opt.db.compress);

	if (err) {
		fprintf(stderr, "Unable to save database: %s\n",
		        strerror(-err));
	}
}

static void handle_shutdown(int sig)
{
	char path[256];
	uint32_t timestamp = interval_timestamp(&opt.archive_interval, 0);

	save_persistent(timestamp);

	if (sig == SIGTERM) {
		snprintf(path, sizeof(path), "%s/0.db", opt.tempdir);
		unlink(path);
	}
	else {
		database_save(gdbh, opt.tempdir, 0, false);
	}

	uloop_done();
	exit(0);
}

static void
handle_commit(struct uloop_timeout *tm)
{
	uint32_t timestamp = interval_timestamp(&opt.archive_interval, 0);

	uloop_timeout_set(tm, opt.commit_interval * 1000);
	save_persistent(timestamp);
}

static void
handle_refresh(struct uloop_timeout *tm)
{
	int err;

	uloop_timeout_set(tm, opt.refresh_interval * 1000);

	err = database_archive(gdbh);

	/* database successfully wrapped around and triggered a ct dump */
	if (err == -ESTALE)
		return;

	/* fatal error during database archiving */
	if (err != 0) {
		fprintf(stderr, "Unable to archive database: %s\n",
				strerror(-err));
		return;
	}

	err = nfnetlink_dump(false);

	if (err) {
		fprintf(stderr, "Unable to dump conntrack: %s\n",
		        strerror(-err));
		return;
	}

	database_save(gdbh, opt.tempdir, 0, false);
}

static int
parse_timearg(const char *val, time_t *dest)
{
	char *e;

	*dest = strtoul(val, &e, 10);

	if (e == val)
		return -EINVAL;

	switch (*e)
	{
	case 'w':
		*dest *= 604800;
		break;

	case 'd':
		*dest *= 86400;
		break;

	case 'h':
		*dest *= 3600;
		break;

	case 'm':
		*dest *= 60;
		break;

	case 's':
	case 0:
		break;

	default:
		return -EINVAL;
	}

	if (*dest > 0x7fffffff / 1000)
		return -ERANGE;

	return 0;
}

static int
server_main(int argc, char **argv)
{
	struct sigaction sa = { .sa_handler = handle_shutdown };
	uint32_t timestamp;
	int optchr, err;
	char *e;

	while ((optchr = getopt(argc, argv, "b:i:r:s:o:p:G:I:L:PZ")) > -1) {
		switch (optchr) {
		case 'b':
			opt.netlink_buffer_size = (int)strtol(optarg, &e, 0);
			if (e == optarg || *e || opt.netlink_buffer_size < 32768) {
				fprintf(stderr, "Invalid netlink buffer size '%s'\n",
				        optarg);
				return 1;
			}
			break;

		case 'i':
			err = parse_timearg(optarg, &opt.commit_interval);
			if (err) {
				fprintf(stderr, "Invalid commit interval '%s': %s\n",
				        optarg, strerror(-err));
				return 1;
			}
			break;

		case 'r':
			err = parse_timearg(optarg, &opt.refresh_interval);
			if (err) {
				fprintf(stderr, "Invalid refresh interval '%s': %s\n",
				        optarg, strerror(-err));
				return 1;
			}
			break;

		case 's':
			err = add_subnet(optarg);
			if (err) {
				fprintf(stderr, "Invalid subnet '%s': %s\n",
				        optarg, strerror(-err));
				return 1;
			}
			break;

		case 'o':
			opt.db.directory = optarg;
			break;

		case 'p':
			opt.protocol_db = optarg;
			break;

		case 'G':
			opt.db.generations = strtoul(optarg, &e, 10);
			if (e == optarg || *e != 0) {
				fprintf(stderr, "Invalid generations argument: %s\n", optarg);
				return 1;
			}
			break;

		case 'I':
			err = interval_pton(optarg, &opt.archive_interval);
			if (err < 0) {
				fprintf(stderr, "Invalid interval '%s': %s\n",
				        optarg, strerror(-err));
				exit(1);
			}
			break;

		case 'P':
			opt.db.prealloc = true;
			break;

		case 'L':
			opt.db.limit = strtoul(optarg, &e, 10);
			if (e == optarg || *e != 0) {
				fprintf(stderr, "Invalid limit argument: %s\n", optarg);
				return 1;
			}
			break;

		case 'Z':
			opt.db.compress = true;
			break;
		}
	}

	if (!opt.archive_interval.type) {
		fprintf(stderr, "No interval specified; assuming 1st of month\n");
		interval_pton("1", &opt.archive_interval);
	}

	uloop_init();

	err = rmkdir(opt.db.directory);

	if (err) {
		fprintf(stderr, "Unable to create database directory: %s\n",
		        strerror(-err));
		return 1;
	}

	database_cleanup();

	gdbh = database_init(&opt.archive_interval, opt.db.prealloc, opt.db.limit);

	if (!gdbh) {
		fprintf(stderr, "Unable to allocate memory database: %s\n",
		        strerror(ENOMEM));
		return 1;
	}

	err = database_load(gdbh, opt.tempdir, 0);

	if (err == -ENOENT) {
		timestamp = interval_timestamp(&opt.archive_interval, 0);
		err = database_load(gdbh, opt.db.directory, timestamp);
	}

	if (err != 0 && err != -ENOENT) {
		fprintf(stderr, "Unable to restore database: %s\n",
		        strerror(-err));
		exit(1);
	}

	err = init_protocols(opt.protocol_db);

	if (err) {
		fprintf(stderr, "Unable to read protocol list %s: %s\n",
		        opt.protocol_db, strerror(-err));
		exit(1);
	}

	err = nfnetlink_connect(opt.netlink_buffer_size);

	if (err) {
		fprintf(stderr, "Unable to connect nfnetlink: %s\n",
		        strerror(-err));
		exit(1);
	}

	err = socket_init(opt.socket);

	if (err) {
		fprintf(stderr, "Unable to create control socket: %s\n",
		        strerror(-err));
		exit(1);
	}

	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);

	commit_tm.cb = handle_commit;
	uloop_timeout_set(&commit_tm, opt.commit_interval * 1000);

	refresh_tm.cb = handle_refresh;
	uloop_timeout_set(&refresh_tm, opt.refresh_interval * 1000);

	uloop_run();

	return 0;
}

int
main(int argc, char **argv)
{
	const char *name = basename(argv[0]);

	if (!strcmp(name, "nlbw"))
		return client_main(argc, argv);

	return server_main(argc, argv);
}
