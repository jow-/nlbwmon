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
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ether.h>

#include "utils.h"


int
rmkdir(const char *path)
{
	char *p, tmp[128];
	struct stat s;

	if (strlen(path) + 1 >= sizeof(tmp))
		return -ENAMETOOLONG;

	snprintf(tmp, sizeof(tmp), "%s/", path);

	for (p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = 0;

			if (stat(tmp, &s) == 0) {
				if (!S_ISDIR(s.st_mode))
					return -ENOTDIR;
			}
			else if (mkdir(tmp, 0750) != 0) {
				return -errno;
			}

			*p = '/';
		}
	}

	return 0;
}


char *
format_macaddr(struct ether_addr *mac)
{
	static char buf[sizeof("ff:ff:ff:ff:ff:ff\0")];

	snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
	         mac->ether_addr_octet[0], mac->ether_addr_octet[1],
	         mac->ether_addr_octet[2], mac->ether_addr_octet[3],
	         mac->ether_addr_octet[4], mac->ether_addr_octet[5]);

	return buf;
}

char *
format_ipaddr(int family, void *addr)
{
	struct in_addr in;
	static char buf[INET6_ADDRSTRLEN];

	if (family == AF_INET) {
		in.s_addr = be32toh(((struct in_addr *)addr)->s_addr);
		inet_ntop(family, &in, buf, sizeof(buf));
	}
	else {
		inet_ntop(family, addr, buf, sizeof(buf));
	}

	return buf;
}
