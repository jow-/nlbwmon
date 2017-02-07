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

#ifndef __SUBNETS_H__
#define __SUBNETS_H__

#include <stdint.h>
#include <netinet/in.h>

#include <libubox/list.h>


struct subnet {
	struct list_head list;
	uint8_t family;
	union {
		struct in_addr in;
		struct in6_addr in6;
	} saddr;
	union {
		struct in_addr in;
		struct in6_addr in6;
	} smask;
};

int add_subnet(const char *addr);
int match_subnet(int family, struct in6_addr *addr);

#endif /* __SUBNETS_H__ */
