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

#include <endian.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libubox/avl.h>

#include <zlib.h>

#include "nlbwmon.h"
#include "database.h"
#include "nfnetlink.h"


struct dbhandle *gdbh = NULL;

static int
database_cmp_index(const void *k1, const void *k2, void *ptr)
{
	return memcmp(k1, k2, offsetof(struct record, count));
}


static void
database_reindex(struct dbhandle *h)
{
	struct record *ptr;
	int i;

	avl_init(&h->index, h->index.comp, h->index.allow_dups, h->index.cmp_ptr);

	for (i = 0; i < db_entries(h->db); i++) {
		ptr = &h->db->records[i];
		ptr->node.key = ptr;
		avl_insert(&h->index, &ptr->node);
	}
}

void
database_reorder(struct dbhandle *h, avl_tree_comp sort_fn, void *sort_ptr)
{
	h->index.comp = sort_fn;
	h->index.cmp_ptr = sort_ptr;
	database_reindex(h);
}

struct record *
database_next(struct dbhandle *h, struct record *cur)
{
	struct record *last = avl_last_element(&h->index, last, node);
	struct record *next = cur ? avl_next_element(cur, node)
	                          : avl_first_element(&h->index, cur, node);

	if (next->node.list.prev != &last->node.list)
		return next;

	return NULL;
}


static struct dbhandle *
database_alloc(avl_tree_comp key_fn, void *key_ptr, bool prealloc,
               uint32_t limit)
{
	struct dbhandle *h;
	uint32_t size;
	size_t len;

	size = 100;

	if (prealloc)
		size = limit;
	else if (limit > 0 && limit < size)
		size = limit;

	len = sizeof(struct database) + size * sizeof(struct record);
	h = calloc(1, sizeof(*h));

	if (!h)
		return NULL;

	h->db = calloc(1, len);

	if (!h->db) {
		free(h);
		return NULL;
	}

	h->pristine = true;
	h->prealloc = prealloc;
	h->limit = limit;
	h->size = size;

	avl_init(&h->index, key_fn, false, key_ptr);

	return h;
}

static int
database_grow(struct dbhandle *h)
{
	struct database *tmp;
	uint32_t size;
	size_t len;

	size = h->size + (h->size >> 1);

	if (h->limit > 0 && size > h->limit)
		size = h->limit;

	if (size <= h->size)
		return -ENOSPC;

	len = sizeof(*h->db) + size * sizeof(h->db->records[0]);
	tmp = realloc(h->db, len);

	if (!tmp)
		return -ENOMEM;

	if (tmp != h->db) {
		h->db = tmp;
		database_reindex(h);
	}

	h->size = size;

	return 0;
}

struct dbhandle *
database_init(const struct interval *intv, bool prealloc, uint32_t limit)
{
	struct dbhandle *h;

	/* we cannot preallocate without known limit */
	if (limit == 0)
		prealloc = false;

	/* initialize in memory database */
	h = database_alloc(database_cmp_index, NULL, prealloc, limit);

	if (!h)
		return NULL;

	h->db->magic = htobe32(MAGIC);
	h->db->entries = 0;

	if (intv) {
		h->db->interval = *intv;
		h->db->timestamp = htobe32(interval_timestamp(intv, 0));
	}

	return h;
}

struct dbhandle *
database_mem(avl_tree_comp key_fn, void *key_ptr)
{
	struct dbhandle *h;

	h = database_alloc(key_fn, key_ptr, false, 0);

	if (!h)
		return NULL;

	h->db->entries = 0;

	return h;
}

int
database_insert(struct dbhandle *h, struct record *rec)
{
	int err;
	struct record *ptr;

	err = database_update(h, rec);

	if (err != -ENOENT)
		return err;

	/* grow database if needed */
	if (db_entries(h->db) >= h->size) {
		err = database_grow(h);

		/* database hard limit reached, start overwriting old entries */
		if (err == -ENOSPC) {
			ptr = &h->db->records[h->off++ % h->size];
			avl_delete(&h->index, &ptr->node);

			memset(ptr, 0, sizeof(*ptr));
			memcpy(ptr, rec, db_recsize);

			ptr->node.key = ptr;
			avl_insert(&h->index, &ptr->node);

			return 0;
		}

		if (err < 0)
			return err;
	}

	h->db->entries = htobe32(db_entries(h->db) + 1);

	ptr = &h->db->records[h->off++];

	memset(ptr, 0, sizeof(*ptr));
	memcpy(ptr, rec, db_recsize);

	ptr->node.key = ptr;
	avl_insert(&h->index, &ptr->node);

	return 0;
}

#define add64(x, y) x = htobe64(be64toh(x) + be64toh(y))

int
database_update(struct dbhandle *h, struct record *rec)
{
	struct record *ptr;

	ptr = avl_find_element(&h->index, rec, ptr, node);

	if (ptr) {
		add64(ptr->count, rec->count);
		add64(ptr->in_pkts, rec->in_pkts);
		add64(ptr->in_bytes, rec->in_bytes);
		add64(ptr->out_pkts, rec->out_pkts);
		add64(ptr->out_bytes, rec->out_bytes);

		return 0;
	}

	return -ENOENT;
}

static int
database_gzclose(gzFile gz)
{
	int err;

	if (!gz)
		return -EBADF;

	err = gzclose(gz);

	switch (err)
	{
	case Z_ERRNO:
		return -errno;

	case Z_STREAM_ERROR:
		return -EBADF;

	case Z_MEM_ERROR:
		return -ENOMEM;

	case Z_BUF_ERROR:
		return -EIO;

	case Z_OK:
		return 0;
	}

	return -EINVAL;
}

static int
database_save_gzip(struct dbhandle *h, const char *path, uint32_t timestamp)
{
	struct record *src;
	gzFile gz = NULL;
	int i, fd;

	fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0640);

	if (fd < 0)
		return -errno;

	gz = gzdopen(fd, "wb9");

	if (!gz) {
		close(fd);
		return -ENOMEM;
	}

	if (gzwrite(gz, h->db, sizeof(*h->db)) != sizeof(*h->db))
		goto out;

	for (i = 0; i < db_entries(h->db); i++) {
		src = &h->db->records[i];

		if (gzwrite(gz, src, db_recsize) != db_recsize)
			goto out;
	}

	errno = 0;

out:
	if (gz)
		errno = -database_gzclose(gz);

	return -errno;
}

static int
database_save_mmap(struct dbhandle *h, const char *path, uint32_t timestamp)
{
	struct database *db = MAP_FAILED;
	struct record *src, *dst;
	int i = 0, o = 0, fd;
	size_t len;

	len = db_disksize(h->db);
	fd = open(path, O_CREAT|O_RDWR, 0640);

	if (fd < 0)
		goto out;

	if (ftruncate(fd, len))
		goto out;

	db = mmap(NULL, len, PROT_WRITE, MAP_SHARED, fd, 0);

	if (db == MAP_FAILED)
		goto out;

	memcpy(db, h->db, sizeof(*db));

	for (i = 0; i < db_entries(h->db); i++) {
		src = &h->db->records[i];
		dst = db_diskrecord(db, o++);
		memcpy(dst, src, db_recsize);
	}

	errno = 0;

out:
	if (db != MAP_FAILED)
		munmap(db, len);

	if (fd >= 0)
		close(fd);

	return -errno;
}

int
database_save(struct dbhandle *h, const char *path, uint32_t timestamp,
              bool compress)
{
	uint32_t old_timestamp;
	char file[256];
	struct stat s;
	int err;

	snprintf(file, sizeof(file), "%s/%u.db%s",
	         path, timestamp, compress ? ".gz" : "");

	/* If the database is pristine (was not read from disk), there must
	 * not be an existing database file already. If there is a file now
	 * which was not present when setting up the database, we're likely
	 * dealing with a storage location that only became available after
	 * nlbwmon started.
	 *
	 * Return -EEXIST to the caller in such a case to allow it to take
	 * appropriate actions, such as emitting a warning or merging the
	 * on-disk data.
	 */
	if (h->pristine && timestamp > 0 && stat(file, &s) == 0)
		return -EEXIST;

	old_timestamp = h->db->timestamp;
	h->db->timestamp = htobe32(timestamp);

	if (compress)
		err = database_save_gzip(h, file, timestamp);
	else
		err = database_save_mmap(h, file, timestamp);

	if (err)
		unlink(file);

	if (timestamp > 0)
		h->pristine = false;

	h->db->timestamp = old_timestamp;

	return err;
}

static int
database_restore_gzip(struct dbhandle *h, const char *path, uint32_t timestamp)
{
	struct database hdr;
	struct record rec;
	uint32_t entries;
	gzFile gz;
	int i;

	gz = gzopen(path, "rb");

	if (!gz)
		return -errno;

	if (gzread(gz, &hdr, sizeof(hdr)) != sizeof(hdr)) {
		database_gzclose(gz);
		return -ERANGE;
	}

	entries = db_entries(&hdr);

	if (h && h->limit > 0 && h->limit < entries)
		entries = h->limit;

	if (be32toh(hdr.magic) != MAGIC) {
		database_gzclose(gz);
		return -EINVAL;
	}

	if (hdr.interval.type == 0 || db_timestamp(&hdr) != timestamp) {
		database_gzclose(gz);
		return -EINVAL;
	}

	if (h) {
		h->pristine = false;

		for (i = 0; i < entries; i++) {
			if (gzread(gz, &rec, db_recsize) != db_recsize) {
				database_gzclose(gz);
				return -ERANGE;
			}

			database_insert(h, &rec);
		}

		if (gzgetc(gz) != -1) {
			database_gzclose(gz);
			return -ERANGE;
		}
	}

	return database_gzclose(gz);
}

static int
database_restore_mmap(struct dbhandle *h, const char *path, uint32_t timestamp,
                      size_t filesize)
{
	struct database *db = MAP_FAILED;
	int i, entries, fd = -1;
	size_t len = filesize;

	if (filesize < sizeof(struct database))
		return -ERANGE;

	if (h && h->limit > 0)
		len = sizeof(struct database) + h->limit * db_recsize;

	if (len > filesize)
		len = filesize;

	fd = open(path, O_RDONLY);

	if (fd < 0)
		goto out;

	db = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);

	if (db == MAP_FAILED)
		goto out;

	entries = db_entries(db);

	if (h && h->limit > 0 && h->limit < entries)
		entries = h->limit;

	if (be32toh(db->magic) != MAGIC) {
		errno = EINVAL;
		goto out;
	}

	if (db->interval.type == 0 || db_timestamp(db) != timestamp) {
		errno = EINVAL;
		goto out;
	}

	if (!h) {
		errno = 0;
		goto out;
	}

	if (sizeof(*db) + entries * db_recsize > len) {
		errno = ERANGE;
		goto out;
	}

	errno = 0;
	h->pristine = false;

	for (i = 0; i < entries; i++)
		database_insert(h, db_diskrecord(db, i));

out:
	if (db != MAP_FAILED)
		munmap(db, len);

	if (fd >= 0)
		close(fd);

	return -errno;
}

int
database_load(struct dbhandle *h, const char *path, uint32_t timestamp)
{
	char name[256];
	struct stat s;

	snprintf(name, sizeof(name), "%s/%u.db.gz", path, timestamp);

	if (!stat(name, &s))
		return database_restore_gzip(h, name, timestamp);

	snprintf(name, sizeof(name), "%s/%u.db", path, timestamp);

	if (!stat(name, &s))
		return database_restore_mmap(h, name, timestamp, s.st_size);

	return -errno;
}

int
database_cleanup(void)
{
	uint32_t timestamp, num;
	struct dirent *entry;
	char *e, path[256];
	DIR *d;

	if (!opt.db.generations)
		return 0;

	d = opendir(opt.db.directory);

	if (!d)
		return -errno;

	errno = 0;
	timestamp = interval_timestamp(&opt.archive_interval, -opt.db.generations);

	while ((entry = readdir(d)) != NULL) {
		if (entry->d_type != DT_REG)
			continue;

		num = strtoul(entry->d_name, &e, 10);

		if (e == entry->d_name || *e != '.')
			continue;

		if (strcmp(e, ".db") != 0 && strcmp(e, ".db.gz") != 0)
			continue;

		if (num < 20000101 || num > timestamp)
			continue;

		snprintf(path, sizeof(path), "%s/%u%s", opt.db.directory, num, e);

		if (unlink(path))
			fprintf(stderr, "Unable to delete %s: %s\n", path, strerror(errno));
	}

	closedir(d);

	return -errno;
}

int
database_archive(struct dbhandle *h)
{
	uint32_t next_ts = interval_timestamp(&h->db->interval, 0);
	uint32_t curr_ts = db_timestamp(h->db);
	int err;

	if (next_ts > curr_ts) {
		err = database_save(h, opt.db.directory, curr_ts, opt.db.compress);

		if (err)
			return err;

		/* lazily reset database, don't (re)alloc */
		h->off = 0;
		h->db->entries = 0;
		h->db->timestamp = htobe32(next_ts);

		database_reindex(h);

		/* carry over yet open streams to new database */
		err = nfnetlink_dump(true);

		if (err)
			return err;

		return -ESTALE;
	}

	return 0;
}

void
database_free(struct dbhandle *h)
{
	free(h->db);
	free(h);
}
