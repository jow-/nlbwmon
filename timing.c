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
#include <stdlib.h>
#include <stdbool.h>
#include <endian.h>
#include <errno.h>

#include "timing.h"


static int
is_leapyear(int year)
{
	if (!(year % 400))
		return 1;

	if (!(year % 100))
		return 0;

	if (!(year % 4))
		return 1;

	return 0;
}

static int
days_in_month(int year, int month)
{
	switch (month) {
	case 1:
	case 3:
	case 5:
	case 7:
	case 8:
	case 10:
	case 12:
		return 31;

	case 2:
		return 28 + is_leapyear(year);

	case 4:
	case 6:
	case 9:
	case 11:
		return 30;
	}

	return -ERANGE;
}

static void
tm_inc_dec(struct tm *tm, bool inc)
{
	if (!inc) {
		if (tm->tm_mon > 0) {
			tm->tm_mon--;
		}
		else {
			tm->tm_mon = 11;
			tm->tm_year--;
		}
	}
	else {
		if (tm->tm_mon < 11) {
			tm->tm_mon++;
		}
		else {
			tm->tm_mon = 0;
			tm->tm_year++;
		}
	}
}

static int
interval_timestamp_monthly(const struct interval *intv, int offset)
{
	time_t now = time(NULL);
	struct tm *loc = localtime(&now);
	int date = (int32_t)be32toh(intv->value);
	int monthdays;

	while (offset != 0) {
		if (offset < 0) {
			tm_inc_dec(loc, false);
			offset++;
		}
		else {
			tm_inc_dec(loc, true);
			offset--;
		}
	}

	monthdays = days_in_month(loc->tm_year + 1900, loc->tm_mon + 1);

	if (date > 0) {
		if (loc->tm_mday < date)
			tm_inc_dec(loc, false);
	}
	else {
		if (loc->tm_mday < (date + monthdays))
			tm_inc_dec(loc, false);

		monthdays = days_in_month(loc->tm_year + 1900, loc->tm_mon + 1);
		date += monthdays;

		if (date < 1)
			date = 1;

		if (date > monthdays)
			date = monthdays;
	}

	return ((loc->tm_year + 1900) * 10000 +
	        (loc->tm_mon  +    1) *   100 +
	         date);
}

static int
interval_timestamp_fixed(const struct interval *intv, int offset)
{
	time_t now, base;
	struct tm *loc;
	int32_t value;

	base = (time_t)be64toh(intv->base);
	value = (int32_t)be32toh(intv->value);

	now = time(NULL);
	now -= now % 86400;
	now += offset * (value * 86400);

	base = now - ((now - base) % (value * 86400));
	loc = localtime(&base);

	return ((loc->tm_year + 1900) * 10000 +
	        (loc->tm_mon  +    1) *   100 +
	         loc->tm_mday);
}

int
interval_pton(const char *spec, struct interval *intv)
{
	unsigned int year, month, mday;
	struct tm loc = { };
	time_t base;
	int value;
	char *e;

	if (sscanf(spec, "%4u-%2u-%2u/%d", &year, &month, &mday, &value) == 4) {
		if (year < 2000 || year > 3000)
			return -ERANGE;

		if (month == 0 || month > 12)
			return -ERANGE;

		if (mday == 0 || mday > days_in_month(year, month))
			return -ERANGE;

		if (value <= 0)
			return -ERANGE;

		loc.tm_isdst = -1;
		loc.tm_mday = mday;
		loc.tm_mon = month - 1;
		loc.tm_year = year - 1900;

		base = mktime(&loc);
		base -= base % 86400;

		intv->type  = FIXED;
		intv->value = htobe32(value);
		intv->base  = htobe64(base);

		return 0;
	}

	value = strtol(spec, &e, 10);

	if (e == spec || *e != 0)
		return -EINVAL;

	intv->type  = MONTHLY;
	intv->base  = 0;
	intv->value = htobe32(value);

	return 0;
}

void
interval_ntop(const struct interval *intv, char *spec, size_t len)
{
	struct tm *loc;
	time_t base;

	switch (intv->type)
	{
	case FIXED:
		base = (time_t)be64toh(intv->base);
		loc = localtime(&base);
		snprintf(spec, len, "%04d-%02d-%02d/%d",
		         loc->tm_year + 1900, loc->tm_mon + 1, loc->tm_mday,
		         intv->value);
		break;

	case MONTHLY:
		snprintf(spec, len, "%d", (int32_t)be32toh(intv->value));
		break;
	}
}

int
interval_timestamp(const struct interval *intv, int offset)
{
	switch (intv->type)
	{
	case FIXED:
		return interval_timestamp_fixed(intv, offset);

	case MONTHLY:
		return interval_timestamp_monthly(intv, offset);
	}

	return -EINVAL;
}
