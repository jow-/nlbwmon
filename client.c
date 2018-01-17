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
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <endian.h>
#include <netdb.h>

#include <libubox/avl.h>
#include <libubox/usock.h>

#include "client.h"
#include "database.h"
#include "protocol.h"
#include "utils.h"

#include "nlbwmon.h"

#define cmp_range(p1, p2, s, e) \
	memcmp(p1 + offsetof(struct record, s), \
	       p2 + offsetof(struct record, s), \
	       (offsetof(struct record, e) + sizeof(((struct record *)0)->e)) - \
	        offsetof(struct record, s))

struct field {
	const char *name;
	size_t off;
	size_t len;
};

#define f(n, m) \
	{ n, offsetof(struct record, m), sizeof(((struct record *)0)->m) }

enum {
	FAMILY   =  0,
	PROTO    =  1,
	PORT     =  2,
	MAC      =  3,
	IP       =  4,
	CONNS    =  5,
	RX_BYTES =  6,
	RX_PKTS  =  7,
	TX_BYTES =  8,
	TX_PKTS  =  9,

	HOST     = 10,
	LAYER7   = 11,

	MAX      = 12
};

static struct field fields[MAX] = {
	[FAMILY]   = f("family",   family),
	[PROTO]    = f("proto",    proto),
	[PORT]     = f("port",     dst_port),
	[MAC]      = f("mac",      src_mac),
	[IP]       = f("ip",       src_addr),
	[CONNS]    = f("conns",    count),
	[RX_BYTES] = f("rx_bytes", in_bytes),
	[RX_PKTS]  = f("rx_pkts",  in_pkts),
	[TX_BYTES] = f("tx_bytes", out_bytes),
	[TX_PKTS]  = f("tx_pkts",  out_pkts),

	[HOST]     = { "host", offsetof(struct record, src_mac),
	  offsetof(struct record, count) - offsetof(struct record, src_mac) },

	[LAYER7]   = { "layer7", offsetof(struct record, proto),
	  offsetof(struct record, src_mac) - offsetof(struct record, proto) }
};


static struct {
	int timestamp;
	bool plain_numbers;
	int8_t group_by[1 + MAX];
	int8_t order_by[1 + MAX];
	char separator;
	char escape;
	char quote;
} client_opt = {
	.separator = '\t',
	.escape = '"',
	.quote = '"',
};

struct command {
	const char *cmd;
	int (*fn)(void);
};


static int
cmp_fn(const void *k1, const void *k2, void *ptr)
{
	int8_t i, n, r, *group = ptr;
	struct field *f;
	int diff;

	for (i = 0; i < group[0]; i++) {
		r = (group[1 + i] < 0);
		n = (r ? -group[1 + i] : group[1 + i]) - 1;
		f = &fields[n];

		diff = memcmp(k1 + f->off, k2 + f->off, f->len);

		if (diff != 0)
			return r ? -diff : diff;
	}

	return 0;
}

static int
sort_fn(const void *k1, const void *k2, void *ptr)
{
	int diff = cmp_fn(k1, k2, ptr);

	if (diff != 0)
		return diff;

	return memcmp(k1, k2, db_recsize);
}

static char *
format_num(uint64_t n)
{
	uint64_t e = 0x1000000000000000;
	const char *unit = "EPTGMK";
	static char buf[10];

	n = be64toh(n);

	if (!client_opt.plain_numbers) {
		while (*unit) {
			if (n > e) {
				snprintf(buf, sizeof(buf), "%4"PRIu64".%02"PRIu64" %c",
				         n / e, (n % e) * 100 / e, *unit);
				return buf;
			}

			unit++;
			e /= 1024;
		}
	}

	snprintf(buf, sizeof(buf), "%8"PRIu64" ", n);
	return buf;
}

static char *
format_proto(uint8_t prnum)
{
	struct protoent *pr = getprotobynumber(prnum);
	static char prstr[11];
	char *p;

	if (pr && pr->p_name) {
		snprintf(prstr, sizeof(prstr), "%s",
		         pr->p_aliases[0] ? pr->p_aliases[0] : pr->p_name);
		for (p = prstr; *p; p++)
			if (*p >= 'a' && *p <= 'z')
				*p -= ('a' - 'A');
	}
	else if (prnum > 0) {
		snprintf(prstr, sizeof(prstr), "%u", prnum);
	}
	else {
		snprintf(prstr, sizeof(prstr), "   unspec.");
	}

	endprotoent();

	return prstr;
}

static void
print_csv_str(const char *str)
{
	if (client_opt.quote)
		putchar(client_opt.quote);

	while (*str) {
		if (*str == client_opt.escape)
			putchar(client_opt.escape);

		putchar(*str++);
	}

	if (client_opt.quote)
		putchar(client_opt.quote);
}

static int
recv_database(struct dbhandle **h)
{
	int i, len, err, ctrl_socket;
	struct database db;
	struct record rec;
	char req[sizeof("dump YYYYMMDD\0")];

	ctrl_socket = usock(USOCK_UNIX, opt.socket, NULL);

	if (!ctrl_socket)
		return -errno;

	len = snprintf(req, sizeof(req), "dump %d", client_opt.timestamp);

	if (send(ctrl_socket, req, len, 0) != len) {
		close(ctrl_socket);
		return -errno;
	}

	if (recv(ctrl_socket, &db, sizeof(db), 0) != sizeof(db)) {
		close(ctrl_socket);
		return -ENODATA;
	}

	*h = database_mem(cmp_fn, client_opt.group_by);

	if (!*h) {
		close(ctrl_socket);
		return -ENOMEM;
	}

	for (i = 0; i < db_entries(&db); i++) {
		if (recv(ctrl_socket, &rec, db_recsize, 0) != db_recsize) {
			close(ctrl_socket);
			return -ENODATA;
		}

		err = database_insert(*h, &rec);

		if (err != 0) {
			close(ctrl_socket);
			return err;
		}
	}

	database_reorder(*h, sort_fn, client_opt.order_by);

	close(ctrl_socket);
	return 0;
}

static int
handle_show(void)
{
	struct dbhandle *h = NULL;
	struct record *rec = NULL;
	char columns[MAX] = { };
	struct protocol *pr;
	int8_t i, r, n;
	int err;

	err = recv_database(&h);

	if (err != 0)
		return err;

	for (i = 0; i < client_opt.group_by[0]; i++)
		columns[client_opt.group_by[1 + i] - 1] = ' ';

	columns[CONNS]    = ' ';
	columns[RX_BYTES] = ' ';
	columns[RX_PKTS]  = ' ';
	columns[TX_BYTES] = ' ';
	columns[TX_PKTS]  = ' ';

	for (i = 0; i < client_opt.order_by[0]; i++) {
		r = (client_opt.order_by[1 + i] < 0);
		n = (r ? -client_opt.order_by[1 + i] : client_opt.order_by[1 + i]) - 1;
		columns[n] = r ? '>' : '<';
	}

	if (columns[FAMILY])
		printf("%c Fam ", columns[FAMILY]);

	if (columns[HOST]) {
		printf("         %c Host (    MAC )  ", columns[HOST]);
	}
	else {
		if (columns[MAC])
			printf("            %c MAC  ", columns[MAC]);

		if (columns[IP])
			printf("           %c IP  ", columns[IP]);
	}

	if (columns[LAYER7]) {
		printf("  %c Layer7  ", columns[LAYER7]);
	}
	else {
		if (columns[PROTO])
			printf("   %c Proto  ", columns[PROTO]);

		if (columns[PORT])
			printf("%c Port ", columns[PORT]);
	}

	printf("  %c Conn.   %c Downld. ( %c Pkts. )    %c Upload ( %c Pkts. )\n",
	       columns[CONNS],
	       columns[RX_BYTES], columns[RX_PKTS],
	       columns[TX_BYTES], columns[TX_PKTS]);

	while ((rec = database_next(h, rec)) != NULL) {
		if (columns[FAMILY])
			printf("IPv%d  ", rec->family == AF_INET ? 4 : 6);

		if (columns[HOST]) {
			printf("%15s (%02x:%02x:%02x)  ",
			       format_ipaddr(rec->family, &rec->src_addr),
			       rec->src_mac.ea.ether_addr_octet[3],
			       rec->src_mac.ea.ether_addr_octet[4],
			       rec->src_mac.ea.ether_addr_octet[5]);
		}
		else {
			if (columns[MAC])
				printf("%17s  ", format_macaddr(&rec->src_mac.ea));

			if (columns[IP])
				printf("%15s  ", format_ipaddr(rec->family, &rec->src_addr));
		}

		if (columns[LAYER7]) {
			pr = lookup_protocol(rec->proto, be16toh(rec->dst_port));
			printf("%10s  ", pr ? pr->name : "other");
		}
		else {
			if (columns[PROTO])
				printf("%10s  ", format_proto(rec->proto));

			if (columns[PORT])
				printf("%5u  ", be16toh(rec->dst_port));
		}

		printf("%s  ",   format_num(rec->count));
		printf("%sB ",   format_num(rec->in_bytes));
		printf("(%s)  ", format_num(rec->in_pkts));
		printf("%sB ",   format_num(rec->out_bytes));
		printf("(%s)\n", format_num(rec->out_pkts));
	}

	database_free(h);

	return 0;
}

static int
handle_json(void)
{
	struct dbhandle *h = NULL;
	struct record *rec = NULL;
	char columns[MAX] = { };
	struct protocol *pr;
	int8_t i, r, n;
	int err;

	err = recv_database(&h);

	if (err != 0)
		return err;

	for (i = 0; i < client_opt.group_by[0]; i++)
		columns[client_opt.group_by[1 + i] - 1] = 1;

	if (columns[HOST]) {
		columns[IP] = columns[MAC] = 1;
		columns[HOST] = 0;
	}

	if (columns[LAYER7]) {
		columns[PROTO] = columns[PORT] = 1;
	}

	columns[CONNS]    = 1;
	columns[RX_BYTES] = 1;
	columns[RX_PKTS]  = 1;
	columns[TX_BYTES] = 1;
	columns[TX_PKTS]  = 1;

	printf("{\"columns\":[");

	for (i = 0, n = 0, r = 0; i < MAX; i++) {
		if (!columns[i])
			continue;

		if (n++)
			printf(",");

		printf("\"%s\"", fields[i].name);
	}

	printf("],\"data\":[");

	while ((rec = database_next(h, rec)) != NULL) {
		if (!r)
			r++;
		else
			printf(",");

		printf("[");

		for (i = 0, n = 0; i < MAX; i++) {
			if (!columns[i])
				continue;

			if (n++)
				printf(",");

			switch (i)
			{
			case FAMILY:
				printf("%"PRIu8, rec->family == AF_INET ? 4 : 6);
				break;

			case PROTO:
				printf("\"%s\"", format_proto(rec->proto));
				break;

			case PORT:
				printf("%"PRIu16, be16toh(rec->dst_port));
				break;

			case LAYER7:
				pr = lookup_protocol(rec->proto, be16toh(rec->dst_port));
				if (pr)
					printf("\"%s\"", pr->name);
				else
					printf("null");
				break;

			case MAC:
				printf("\"%s\"", format_macaddr(&rec->src_mac.ea));
				break;

			case IP:
				printf("\"%s\"", format_ipaddr(rec->family, &rec->src_addr));
				break;

			case CONNS:
				printf("%"PRIu64, be64toh(rec->count));
				break;

			case RX_BYTES:
				printf("%"PRIu64, be64toh(rec->in_bytes));
				break;

			case RX_PKTS:
				printf("%"PRIu64, be64toh(rec->in_pkts));
				break;

			case TX_BYTES:
				printf("%"PRIu64, be64toh(rec->out_bytes));
				break;

			case TX_PKTS:
				printf("%"PRIu64, be64toh(rec->out_pkts));
				break;
			}
		}

		printf("]");
	}

	database_free(h);

	printf("]}");

	return 0;
}

static int
handle_csv(void)
{
	struct dbhandle *h = NULL;
	struct record *rec = NULL;
	char columns[MAX] = { };
	struct protocol *pr;
	int8_t i, n;
	int err;

	err = recv_database(&h);

	if (err != 0)
		return err;

	for (i = 0; i < client_opt.group_by[0]; i++)
		columns[client_opt.group_by[1 + i] - 1] = 1;

	if (columns[HOST]) {
		columns[IP] = columns[MAC] = 1;
		columns[HOST] = 0;
	}

	if (columns[LAYER7]) {
		columns[PROTO] = columns[PORT] = 1;
	}

	columns[CONNS]    = 1;
	columns[RX_BYTES] = 1;
	columns[RX_PKTS]  = 1;
	columns[TX_BYTES] = 1;
	columns[TX_PKTS]  = 1;

	for (i = 0, n = 0; i < MAX; i++) {
		if (!columns[i])
			continue;

		if (n++)
			putchar(client_opt.separator);

		print_csv_str(fields[i].name);
	}

	putchar('\n');

	while ((rec = database_next(h, rec)) != NULL) {
		for (i = 0, n = 0; i < MAX; i++) {
			if (!columns[i])
				continue;

			if (n++)
				putchar(client_opt.separator);

			switch (i)
			{
			case FAMILY:
				printf("%"PRIu8, rec->family == AF_INET ? 4 : 6);
				break;

			case PROTO:
				print_csv_str(format_proto(rec->proto));
				break;

			case PORT:
				printf("%"PRIu16, be16toh(rec->dst_port));
				break;

			case LAYER7:
				pr = lookup_protocol(rec->proto, be16toh(rec->dst_port));
				if (pr)
					print_csv_str(pr->name);
				break;

			case MAC:
				print_csv_str(format_macaddr(&rec->src_mac.ea));
				break;

			case IP:
				print_csv_str(format_ipaddr(rec->family, &rec->src_addr));
				break;

			case CONNS:
				printf("%"PRIu64, be64toh(rec->count));
				break;

			case RX_BYTES:
				printf("%"PRIu64, be64toh(rec->in_bytes));
				break;

			case RX_PKTS:
				printf("%"PRIu64, be64toh(rec->in_pkts));
				break;

			case TX_BYTES:
				printf("%"PRIu64, be64toh(rec->out_bytes));
				break;

			case TX_PKTS:
				printf("%"PRIu64, be64toh(rec->out_pkts));
				break;
			}
		}

		putchar('\n');
	}

	database_free(h);

	return 0;
}

static int
handle_list(void)
{
	int ctrl_socket;

	ctrl_socket = usock(USOCK_UNIX, opt.socket, NULL);

	if (!ctrl_socket)
		return -errno;

	if (send(ctrl_socket, "list", 4, 0) != 4) {
		close(ctrl_socket);
		return -errno;
	}

	while (true) {
		if (recv(ctrl_socket, &client_opt.timestamp,
		         sizeof(client_opt.timestamp), 0) <= 0)
			break;

		printf("%04d-%02d-%02d\n",
		       client_opt.timestamp / 10000,
		       client_opt.timestamp % 10000 / 100,
		       client_opt.timestamp % 100);
	}

	close(ctrl_socket);

	return 0;
}

static int
handle_commit(void)
{
	char reply[128] = { };
	int ctrl_socket;

	ctrl_socket = usock(USOCK_UNIX, opt.socket, NULL);

	if (!ctrl_socket)
		return -errno;

	if (send(ctrl_socket, "commit", 6, 0) != 6) {
		close(ctrl_socket);
		return -errno;
	}

	if (recv(ctrl_socket, reply, sizeof(reply)-1, 0) <= 0) {
		close(ctrl_socket);
		return -ENODATA;
	}

	printf("%s\n", reply);
	close(ctrl_socket);

	return -strtol(reply, NULL, 10);
}

static struct command commands[] = {
	{ "show", handle_show },
	{ "json", handle_json },
	{ "csv", handle_csv },
	{ "list", handle_list },
	{ "commit", handle_commit },
};


int
client_main(int argc, char **argv)
{
	struct command *cmd = NULL;
	int i, f, err, optchr;
	unsigned int year, month, day;
	char c, *p;

	while ((optchr = getopt(argc, argv, "c:p:S:g:o:t:s::q::e::n")) > -1) {
		switch (optchr) {
		case 'S':
			opt.socket = optarg;
			break;

		case 'c':
			for (i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
				if (!strcmp(commands[i].cmd, optarg)) {
					cmd = &commands[i];
					break;
				}
			}

			if (!cmd) {
				fprintf(stderr, "Unrecognized command '%s'\n", optarg);
				return 1;
			}

			break;

		case 'p':
			opt.protocol_db = optarg;
			break;

		case 'g':
		case 'o':
			p = optarg;

			while (1) {
				c = *p++;

				if (c != ',' && c != '\0')
					continue;

				for (i = 0, f = 0; i < MAX; i++) {
					if (*optarg == '-') {
						if (optchr == 'g') {
							fprintf(stderr, "Cannot invert group column\n");
							return 1;
						}

						if (!strncmp(fields[i].name, optarg+1, p-optarg-2)) {
							f = -(1 + i);
							break;
						}
					}
					else if (!strncmp(fields[i].name, optarg, p-optarg-1)) {
						f = 1 + i;
						break;
					}
				}

				if (!f) {
					fprintf(stderr, "Unrecognized field '%s'\n", optarg);
					return 1;
				}

				if (optchr == 'g')
					client_opt.group_by[++(client_opt.group_by[0])] = f;
				else
					client_opt.order_by[++(client_opt.order_by[0])] = f;

				if (c == '\0')
					break;

				optarg = p;
			}

			break;

		case 't':
			if (sscanf(optarg, "%4u-%2u-%2u", &year, &month, &day) != 3) {
				fprintf(stderr, "Unrecognized date '%s'\n", optarg);
				return 1;
			}

			client_opt.timestamp = year * 10000 + month * 100 + day;
			break;

		case 'n':
			client_opt.plain_numbers = 1;
			break;

		case 's':
			client_opt.separator = optarg ? *optarg : 0;
			break;

		case 'q':
			client_opt.quote = optarg ? *optarg : 0;
			break;

		case 'e':
			client_opt.escape = optarg ? *optarg : 0;
			break;
		}
	}

	if (!client_opt.group_by[0]) {
		client_opt.group_by[0] = 3;
		client_opt.group_by[1] = FAMILY + 1;
		client_opt.group_by[2] = HOST   + 1;
		client_opt.group_by[3] = LAYER7 + 1;

	}

	if (!client_opt.order_by[0]) {
		client_opt.order_by[0] = 2;
		client_opt.order_by[1] = -RX_BYTES - 1;
		client_opt.order_by[2] = -RX_PKTS  - 1;
	}

	if (!cmd) {
		fprintf(stderr, "No command specified\n");
		return 1;
	}

	err = init_protocols(opt.protocol_db);

	if (err) {
		fprintf(stderr, "Unable to read protocol list %s: %s\n",
		        opt.protocol_db, strerror(-err));
		return 1;
	}

	err = cmd->fn();

	if (err) {
		fprintf(stderr, "Error while processing command: %s\n",
		        strerror(-err));
		return err;
	}

	return 0;
}
