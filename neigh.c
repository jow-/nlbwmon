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

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include <endian.h>

#include <netlink/msg.h>
#include <netlink/attr.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <linux/rtnetlink.h>

#include "neigh.h"

static struct avl_tree neighbors;

static struct nl_sock *rt_sock = NULL;
static struct nl_cb *rt_cb = NULL;
static bool rt_done = false;

static int
cb_done(struct nl_msg *msg, void *arg)
{
	rt_done = true;
	return NL_STOP;
}

static int
cb_error(struct sockaddr_nl *nla, struct nlmsgerr *err, void *arg)
{
	rt_done = true;
	return NL_STOP;
}

static int
rt_connect(void)
{
	int err = -ENOMEM;

	rt_sock = nl_socket_alloc();
	if (!rt_sock)
		goto out;

	rt_cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!rt_cb)
		goto out;

	err = nl_connect(rt_sock, NETLINK_ROUTE);
	if (err < 0)
		goto out;

	nl_cb_set(rt_cb, NL_CB_FINISH, NL_CB_CUSTOM, cb_done, NULL);
	nl_cb_err(rt_cb, NL_CB_CUSTOM, cb_error, NULL);

	return 0;

out:
	if (rt_cb)
		nl_cb_put(rt_cb);

	if (rt_sock)
		nl_socket_free(rt_sock);

	return err;
}


struct neigh_query {
	int family;
	const void *addr;
	struct ether_addr *mac;
};

static int
neigh_parse(struct nl_msg *msg, void *arg)
{
	struct nlmsghdr *hdr = nlmsg_hdr(msg);
	struct ndmsg *nd = NLMSG_DATA(hdr);
	struct nlattr *tb[NDA_MAX+1];

	struct neigh_query *query = arg;
	struct ether_addr empty = { };
	static struct ether_addr res;

	if (hdr->nlmsg_type != RTM_NEWNEIGH || nd->ndm_family != query->family)
		return NL_SKIP;

	if (nd->ndm_state & (NUD_NOARP | NUD_FAILED | NUD_INCOMPLETE))
		return NL_SKIP;

	if (nlmsg_parse(hdr, sizeof(*nd), tb, NDA_MAX, NULL))
		return NL_SKIP;

	if (!tb[NDA_LLADDR] || !tb[NDA_DST])
		return NL_SKIP;

	if (memcmp(nla_data(tb[NDA_DST]), query->addr, nla_len(tb[NDA_DST])))
		return NL_SKIP;

	if (nla_len(tb[NDA_LLADDR]) > sizeof(res))
		return NL_SKIP;

	if (!memcmp(nla_data(tb[NDA_LLADDR]), &empty, nla_len(tb[NDA_LLADDR])))
		return NL_SKIP;

	memset(&res, 0, sizeof(res));
	memcpy(&res, nla_data(tb[NDA_LLADDR]), nla_len(tb[NDA_LLADDR]));
	query->mac = &res;

	return NL_SKIP;
}

static struct ether_addr *
ipaddr_to_macaddr(int family, const void *addr)
{
	struct neigh_query query = { .family = family, .addr = addr };
	struct ndmsg ndm = { .ndm_family = family };
	struct nl_msg *msg = NULL;

	msg = nlmsg_alloc_simple(RTM_GETNEIGH, NLM_F_REQUEST | NLM_F_DUMP);

	if (!msg)
		return NULL;

	nlmsg_append(msg, &ndm, sizeof(ndm), 0);

	rt_done = false;

	nl_cb_set(rt_cb, NL_CB_VALID, NL_CB_CUSTOM, neigh_parse, &query);
	nl_send_auto_complete(rt_sock, msg);
	nlmsg_free(msg);

	while (!rt_done)
		nl_recvmsgs(rt_sock, rt_cb);

	return query.mac;
}


struct ifindex_query {
	int family;
	const void *addr;
	int ifindex;
};

static int
ipaddr_parse(struct nl_msg *msg, void *arg)
{
	struct nlmsghdr *hdr = nlmsg_hdr(msg);
	struct ifaddrmsg *ifa;
	struct nlattr *addr, *tb[__IFA_MAX+1];
	struct ifindex_query *query = arg;
	int len = hdr->nlmsg_len;

	for (; nlmsg_ok(hdr, len); hdr = nlmsg_next(hdr, &len)) {
		if (hdr->nlmsg_type != RTM_NEWADDR)
			continue;

		ifa = nlmsg_data(hdr);

		if (ifa->ifa_family != query->family)
			continue;

		if (nlmsg_parse(hdr, sizeof(*ifa), tb, __IFA_MAX, NULL))
			continue;

		addr = tb[IFA_LOCAL] ? tb[IFA_LOCAL] : tb[IFA_ADDRESS];

		if (!addr || memcmp(nla_data(addr), query->addr, nla_len(addr)))
			continue;

		query->ifindex = ifa->ifa_index;
	}

	return NL_SKIP;
}

static int
ipaddr_to_ifindex(int family, const void *addr)
{
	struct ifindex_query query = { .family = family, .addr = addr };
	struct ifaddrmsg ifa = { .ifa_family = family };
	struct nl_msg *msg = NULL;

	msg = nlmsg_alloc_simple(RTM_GETADDR, NLM_F_REQUEST | NLM_F_DUMP);

	if (!msg)
		return -1;

	nlmsg_append(msg, &ifa, sizeof(ifa), 0);

	rt_done = false;

	nl_cb_set(rt_cb, NL_CB_VALID, NL_CB_CUSTOM, ipaddr_parse, &query);
	nl_send_auto_complete(rt_sock, msg);
	nlmsg_free(msg);

	while (!rt_done)
		nl_recvmsgs(rt_sock, rt_cb);

	return query.ifindex;
}


static struct ether_addr *
link_parse(void *msg, int len)
{
	static struct ether_addr mac;

	struct nlattr *tb[__IFLA_MAX+1];
	struct ifinfomsg *ifi;
	struct nlmsghdr *hdr;

	for (hdr = msg; nlmsg_ok(hdr, len); hdr = nlmsg_next(hdr, &len)) {
		if (hdr->nlmsg_type != RTM_NEWLINK)
			continue;

		ifi = nlmsg_data(hdr);

		if (nlmsg_parse(hdr, sizeof(*ifi), tb, __IFLA_MAX, NULL))
			continue;

		if (!tb[IFLA_ADDRESS])
			continue;

		if (nla_len(tb[IFLA_ADDRESS]) > sizeof(mac))
			continue;

		memset(&mac, 0, sizeof(mac));
		memcpy(&mac, RTA_DATA(tb[IFLA_ADDRESS]), nla_len(tb[IFLA_ADDRESS]));

		return &mac;
	}

	return NULL;
}

static struct ether_addr *
ifindex_to_macaddr(int ifindex)
{
	struct ifinfomsg ifi = { .ifi_index = ifindex };
	struct ether_addr *mac = NULL;
	struct nl_msg *msg = NULL;
	struct sockaddr_nl peer;
	unsigned char *reply;
	int len;

	msg = nlmsg_alloc_simple(RTM_GETLINK, NLM_F_REQUEST);

	if (!msg)
		return NULL;

	nlmsg_append(msg, &ifi, sizeof(ifi), 0);
	nl_send_auto_complete(rt_sock, msg);
	nlmsg_free(msg);

	len = nl_recv(rt_sock, &peer, &reply, NULL);

	if (len > 0) {
		mac = link_parse(reply, len);
		free(reply);
	}

	return mac;
}


int
update_macaddr(int family, const void *addr)
{
	struct neigh_entry *ptr, *tmp;
	union neigh_key key = { };
	struct ether_addr *res;
	int ifindex;

	if (family == AF_INET6) {
		key.data.family = AF_INET6;
		key.data.addr.in6 = *(struct in6_addr *)addr;
	}
	else {
		key.data.family = AF_INET;
		key.data.addr.in.s_addr = be32toh(((struct in_addr *)addr)->s_addr);
	}

	res = ipaddr_to_macaddr(family, &key.data.addr);

	if (!res) {
		ifindex = ipaddr_to_ifindex(family, &key.data.addr);

		if (ifindex > 0)
			res = ifindex_to_macaddr(ifindex);

		if (!res)
			return -ENOENT;
	}

	ptr = avl_find_element(&neighbors, &key, tmp, node);

	if (!ptr) {
		ptr = calloc(1, sizeof(*ptr));

		if (!ptr)
			return -ENOMEM;

		ptr->key = key;
		ptr->node.key = &ptr->key;

		avl_insert(&neighbors, &ptr->node);
	}

	ptr->mac = *res;
	return 0;
}

int
lookup_macaddr(int family, const void *addr, struct ether_addr *mac)
{
	struct neigh_entry *ptr, *tmp;
	union neigh_key key = { };

	if (family == AF_INET6) {
		key.data.family = AF_INET6;
		key.data.addr.in6 = *(struct in6_addr *)addr;
	}
	else {
		key.data.family = AF_INET;
		key.data.addr.in.s_addr = be32toh(((struct in_addr *)addr)->s_addr);
	}

	ptr = avl_find_element(&neighbors, &key, tmp, node);

	if (!ptr)
		return -ENOENT;

	*mac = ptr->mac;
	return 0;
}


static int
avl_cmp_neigh(const void *k1, const void *k2, void *ptr)
{
	int i;
	const union neigh_key *a = k1;
	const union neigh_key *b = k2;

	for (i = 0; i < sizeof(a->u32) / sizeof(a->u32[0]); i++)
		if (a->u32[i] != b->u32[i])
			return (a->u32[i] - b->u32[i]);

	return 0;
}

__attribute__((constructor)) static void init_neighbors(void)
{
	rt_connect();
	avl_init(&neighbors, avl_cmp_neigh, false, NULL);
}
