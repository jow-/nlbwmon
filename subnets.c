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

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <endian.h>
#include <arpa/inet.h>

#include <libubox/list.h>

#include "subnets.h"


static LIST_HEAD(subnets);

static int
parse_subnet(const char *addr, struct subnet *net)
{
	char *mask, *e, tmp[INET6_ADDRSTRLEN] = { };
	unsigned long int n;
	uint8_t i, b;

	mask = strchr(addr, '/');

	if (mask)
		memcpy(tmp, addr, mask++ - addr);

	if (inet_pton(AF_INET6, mask ? tmp : addr, &net->saddr.in6)) {
		net->family = AF_INET6;

		if (mask) {
			if (inet_pton(AF_INET6, mask, &net->smask.in6))
				return 0;

			n = strtoul(mask, &e, 10);

			if (e == mask || *e)
				return -EINVAL;

			if (n > 128)
				return -ERANGE;

			for (i = 0; i < sizeof(net->smask.in6.s6_addr); i++) {
				b = (n > 8) ? 8 : n;
				net->smask.in6.s6_addr[i] = (uint8_t)(0xFF << (8 - b));
				n -= b;
			}
		}

		return 0;
	}
	else if (inet_pton(AF_INET, mask ? tmp : addr, &net->saddr.in)) {
		net->family = AF_INET;
		net->saddr.in.s_addr = htobe32(net->saddr.in.s_addr);

		if (mask) {
			if (inet_pton(AF_INET, mask, &net->smask.in)) {
				net->smask.in.s_addr = htobe32(net->smask.in.s_addr);
				return 0;
			}

			n = strtoul(mask, &e, 10);

			if (e == mask || *e)
				return -EINVAL;

			if (n > 32)
				return -ERANGE;

			net->smask.in.s_addr = n ? ~((1 << (32 - n)) - 1) : 0;
		}

		return 0;
	}

	return -EINVAL;
}

int
add_subnet(const char *addr)
{
	struct subnet *net = calloc(1, sizeof(*net));
	int err;

	if (!net)
		return -ENOMEM;

	err = parse_subnet(addr, net);

	if (err != 0) {
		free(net);
		return err;
	}

	list_add_tail(&net->list, &subnets);
	return 0;
}

int
match_subnet(int family, struct in6_addr *addr)
{
	struct subnet *net;
	uint32_t *a, *b, *m;

	if (list_empty(&subnets))
		return -ENOENT;

	list_for_each_entry(net, &subnets, list) {
		a = addr->s6_addr32;
		b = net->saddr.in6.s6_addr32;
		m = net->smask.in6.s6_addr32;

		if (net->family != family)
			continue;

		if (((a[0] & m[0]) != (b[0] & m[0])) ||
		    ((a[1] & m[1]) != (b[1] & m[1])) ||
		    ((a[2] & m[2]) != (b[2] & m[2])) ||
		    ((a[3] & m[3]) != (b[3] & m[3])))
			continue;

		return 0;
	}

	return -ENOENT;
}
