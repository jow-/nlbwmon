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

#ifndef __DATABASE_H__
#define __DATABASE_H__

#include <stdint.h>

#include <netinet/in.h>
#include <netinet/ether.h>

#include <libubox/avl.h>

#include "timing.h"
#include "nlbwmon.h"

#define MAGIC 0x6e6c626d  /* 'nlbm' */

#define db_size(db, n) \
	(sizeof(*(db)) + (n) * sizeof(struct record))

#define db_recsize \
	offsetof(struct record, node)

#define db_entries(db) \
	be32toh((db)->entries)

#define db_timestamp(db) \
	be32toh((db)->timestamp)

#define db_disksize(db) \
	(sizeof(*(db)) + db_entries(db) * db_recsize)

#define db_diskrecord(db, n) \
	(struct record *)((void *)(db)->records + (n) * db_recsize)

#define db_record(db, n) \
	(struct record *)&(db)->records[(n)];


struct record {
	uint8_t family;
	uint8_t proto;
	uint16_t dst_port;
	union {
		struct ether_addr ea;
		uint64_t u64;
	} src_mac;
	union {
		struct in6_addr in6;
		struct in_addr in;
	} src_addr;
	uint64_t count;
	uint64_t out_pkts;
	uint64_t out_bytes;
	uint64_t in_pkts;
	uint64_t in_bytes;
	struct avl_node node;
};

struct database {
	uint32_t magic;
	uint32_t entries;
	uint32_t timestamp;
	struct interval interval;
	struct record records[];
};

struct dbhandle {
	bool prealloc;
	bool pristine;
	uint32_t limit;
	uint32_t size;
	uint32_t off;
	struct avl_tree index;
	struct database *db;
};

extern struct dbhandle *gdbh;

struct dbhandle * database_mem(avl_tree_comp key_fn, void *key_ptr);
struct dbhandle * database_init(const struct interval *intv, bool prealloc,
                                uint32_t limit);

int database_insert(struct dbhandle *h, struct record *rec);
int database_update(struct dbhandle *h, struct record *rec);

void database_reorder(struct dbhandle *h, avl_tree_comp sort_fn,
                      void *sort_ptr);

struct record * database_next(struct dbhandle *h, struct record *prev);

int database_save(struct dbhandle *h, const char *path, uint32_t timestamp,
                  bool compress);

int database_load(struct dbhandle *h, const char *path, uint32_t timestamp);

int database_archive(struct dbhandle *h);
int database_cleanup(void);

void database_free(struct dbhandle *h);

#endif /* __DATABASE_H__ */
