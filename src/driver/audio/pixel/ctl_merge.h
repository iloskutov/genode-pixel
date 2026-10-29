/*
 * \brief  Value text of a 'default'/'control' config node -> element values
 * \author Ivan Loskutov
 * \date   2026-10-01
 *
 * The rules are the ones Android's audio_route applies to the 'value' and
 * 'id' attributes of a mixer path (tool/mixer_paths_to_hid.py lists them):
 *
 * - a node without index sets the whole control: integer and byte controls
 *   take a space-separated list with one number per element, elements the
 *   list does not cover become zero; bool and enum controls take one value
 *   for all elements
 * - a node with index sets that element from the first number (or the enum
 *   item); the other elements keep what the caller put into the array
 * - numbers as strtol with base 0 (bytes: base 16); an enum value is the
 *   text of an item, or its number if no item has that text
 *
 * Free of kernel and Genode dependencies: used by card.c and by the host
 * test in test/.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _PIXEL_AUDIO__CTL_MERGE_H_
#define _PIXEL_AUDIO__CTL_MERGE_H_

enum ctl_merge_type {
	CTL_MERGE_BOOL,
	CTL_MERGE_INT,     /* integer and 64-bit integer controls */
	CTL_MERGE_ENUM,
	CTL_MERGE_BYTES,
};

enum {
	CTL_MERGE_OK        =  0,
	CTL_MERGE_BAD_VALUE = -1,   /* not a number / not an item of the enum */
	CTL_MERGE_BAD_INDEX = -2,   /* index beyond the control's elements    */
};

struct ctl_merge_ctl
{
	enum ctl_merge_type  type;
	unsigned             count;      /* number of elements         */
	char const * const  *items;      /* enum: item texts, may hold NULL */
	unsigned             num_items;
};


/* next space-separated token of 'text', NULL (0) at the end */
static inline char const *ctl_merge_token(char const *text, unsigned *len)
{
	while (*text == ' ')
		text++;

	if (!*text)
		return 0;

	*len = 0;
	while (text[*len] && text[*len] != ' ')
		(*len)++;

	return text;
}


/*
 * Number of 'len' characters at 'tok'. Base 0 detects "0x" (hex) and a
 * leading "0" (octal). Unlike strtol, trailing garbage is an error.
 */
static inline int ctl_merge_number(char const *tok, unsigned len, unsigned base,
                                   long long *out)
{
	unsigned  i = 0, digits = 0;
	int       negative = 0;
	long long value = 0;

	if (i < len && (tok[i] == '-' || tok[i] == '+'))
		negative = tok[i++] == '-';

	if (i + 1 < len && tok[i] == '0' && (tok[i + 1] == 'x' || tok[i + 1] == 'X')
	 && (base == 0 || base == 16)) {
		base = 16;
		i   += 2;
	} else if (base == 0) {
		base = (i < len && tok[i] == '0') ? 8 : 10;
	}

	for (; i < len; i++, digits++) {
		char const c = tok[i];
		unsigned   d;

		if      (c >= '0' && c <= '9') d = (unsigned)(c - '0');
		else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a') + 10;
		else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A') + 10;
		else return CTL_MERGE_BAD_VALUE;

		if (d >= base)
			return CTL_MERGE_BAD_VALUE;

		value = value*base + d;
	}

	if (!digits)
		return CTL_MERGE_BAD_VALUE;

	*out = negative ? -value : value;
	return CTL_MERGE_OK;
}


static inline int ctl_merge_streq(char const *a, char const *b)
{
	while (*a && *a == *b) { a++; b++; }
	return *a == *b;
}


static inline unsigned ctl_merge_strlen(char const *s)
{
	unsigned n = 0;
	while (s[n]) n++;
	return n;
}


/* item of an enum control by its text, or by its number as fallback */
static inline int ctl_merge_enum_item(struct ctl_merge_ctl const *ctl,
                                      char const *text, long long *out)
{
	unsigned  i;
	long long number;

	for (i = 0; ctl->items && i < ctl->num_items; i++) {
		if (ctl->items[i] && ctl_merge_streq(ctl->items[i], text)) {
			*out = i;
			return CTL_MERGE_OK;
		}
	}

	if (ctl_merge_number(text, ctl_merge_strlen(text), 0, &number) == CTL_MERGE_OK
	 && number >= 0 && number < (long long)ctl->num_items) {
		*out = number;
		return CTL_MERGE_OK;
	}
	return CTL_MERGE_BAD_VALUE;
}


/**
 * Merge one node into the element values 'vals' ('ctl->count' entries)
 *
 * \param index  element of the control, negative for the whole control
 */
static inline int ctl_merge_write(struct ctl_merge_ctl const *ctl, int index,
                                  char const *value, long long *vals)
{
	unsigned const base = (ctl->type == CTL_MERGE_BYTES) ? 16 : 0;

	char const *tok;
	unsigned    len = 0, i;
	long long   first = 0;

	if (ctl->type == CTL_MERGE_ENUM) {
		if (ctl_merge_enum_item(ctl, value, &first))
			return CTL_MERGE_BAD_VALUE;
	} else {
		tok = ctl_merge_token(value, &len);
		if (!tok || ctl_merge_number(tok, len, base, &first))
			return CTL_MERGE_BAD_VALUE;
	}

	if (index >= 0) {
		if ((unsigned)index >= ctl->count)
			return CTL_MERGE_BAD_INDEX;
		vals[index] = first;
		return CTL_MERGE_OK;
	}

	if (ctl->type == CTL_MERGE_INT || ctl->type == CTL_MERGE_BYTES) {

		for (i = 0; i < ctl->count; i++)
			vals[i] = 0;

		tok = value;
		for (i = 0; i < ctl->count; i++) {
			tok = ctl_merge_token(tok, &len);
			if (!tok)
				break;
			if (ctl_merge_number(tok, len, base, &vals[i]))
				return CTL_MERGE_BAD_VALUE;
			tok += len;
		}
		return CTL_MERGE_OK;
	}

	for (i = 0; i < ctl->count; i++)
		vals[i] = first;

	return CTL_MERGE_OK;
}

#endif /* _PIXEL_AUDIO__CTL_MERGE_H_ */
