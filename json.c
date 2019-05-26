/*
 * Copyright 2013-2019 Thierry FOURNIER
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License.
 *
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"

inline
void json_init(struct json *json, char *buffer, int len, int do_indent)
{
	json->buffer = buffer,
	json->len = len;
	json->avalaible = json->len;
	json->p = json->buffer;
	json->do_indent = !!do_indent;
	json->level = 0;
	json->stack[0].is_first = 1;
	json->stack[0].elt = JSON_ANY;
	json->stack[0].reserved = 0;
	json->do_write = 1;
}

static inline
void json_reserve(struct json *json, int len)
{
	json->avalaible -= len;
}

static inline
void json_unreserve(struct json *json, int len)
{
	json->avalaible += len;
}

static inline
void json_cat_pchar_noctl(struct json *json, const char *cat, size_t len)
{
	memcpy(json->p, cat, len);
	json->p += len;
	json->avalaible -= len;
}

static inline
void json_add_char_noctl(struct json *json, const char c)
{
	*json->p = c;
	json->p++;
	json->avalaible--;
}

static inline
int json_check_separator_len(struct json *json)
{
	int len = 0;
	int i;

	/* if the current level is JSON_ATTR, do nothing */
	if (json->stack[json->level].elt == JSON_ATTR)
		return 0;

	/* if is the first level, dont print ',' */
	if (!json->stack[json->level].is_first)
		len++;

	/* indent */
	if (json->do_indent) {
		/* if the level is 0 dont print the jump */
		if (json->level > 0)
			len++;
		for (i=0; i<=json->level; i++)
			if (json->stack[i].elt == JSON_OBJECT || json->stack[i].elt == JSON_ARRAY)
				len++;
	}

	return len;
}

static inline
void json_check_separator_write(struct json *json)
{
	int i;

	/* if the current level is JSON_ATTR, do nothing */
	if (json->stack[json->level].elt == JSON_ATTR)
		return;

	/* if is the first level, dont print ',' */
	if (!json->stack[json->level].is_first)
		json_add_char_noctl(json, ',');

	/* indent */
	if (json->do_indent) {
		/* if the level is 0 dont print the jump */
		if (json->level > 0)
			json_add_char_noctl(json, '\n');
		for (i=0; i<=json->level; i++)
			if (json->stack[i].elt == JSON_OBJECT || json->stack[i].elt == JSON_ARRAY)
				json_add_char_noctl(json, '\t');
	}
}

inline
void json_pop(struct json *json)
{
	int i;

	/* check write avalaibility */
	if (!json->do_write)
		return;

	/* if we are in the level 0, do nothing */
	if (json->level <= 0)
		return;

	/* unreserve */
	json_unreserve(json, json->stack[json->level].reserved);

	/* print indentation elements */
	if (json->do_indent) {
		/* if the current element is JSON_STRING or JSON_ATTR, dont indent */
		if (json->stack[json->level].elt != JSON_STRING &&
		    json->stack[json->level].elt != JSON_ATTR) {
			json_add_char_noctl(json, '\n');
			for (i=0; i<=json->level-1; i++)
				if (json->stack[i].elt == JSON_OBJECT ||
				    json->stack[i].elt == JSON_ARRAY)
					json_add_char_noctl(json, '\t');
		}
	}

	/* the closing tag */
	switch (json->stack[json->level].elt) {
	case JSON_OBJECT:
		json_add_char_noctl(json, '}');
		break;
	case JSON_ARRAY:
		json_add_char_noctl(json, ']');
		break;
	case JSON_STRING:
		json_add_char_noctl(json, '"');
		break;
	case JSON_ATTR:
		/* attribute cannot have value, dump the default value */
		if (json->stack[json->level].is_first)
			json_cat_pchar_noctl(json, "null", 4);
		break;
	default:
		break;
	}

	/* raise down the level */
	json->level--;
}

void json_finalize(struct json *json)
{
	/* check write avalaibility */
	if (!json->do_write)
		return;

	if (json->avalaible < 1)
		return;

	if (json->do_indent)
		json_add_char_noctl(json, '\n');
}

static inline
void json_close_all(struct json *json)
{
	while (json->level > 0)
		json_pop(json);
	json->do_write = 0;
}

/* This function return the length of the escaped string.
 * String is defined as:
 *
 *    any-Unicode-character-except-"-or-\-or-control-character
 *    \", \\, \/, \b, \f, \n, \r, \t, \u, four-hex-digits
 */
static inline
int json_escape_char_get_len(const char *data, size_t length)
{
	unsigned int i;
	unsigned int len = 0;

	for (i=0; i<length; i++) {
		/**/ if (data[i] == '"')
			len += 2;
		else if (data[i] == '\\')
			len += 2;
		else if (data[i] == '/')
			len += 2;
		else if (data[i] == '\b')
			len += 2;
		else if (data[i] == '\f')
			len += 2;
		else if (data[i] == '\r')
			len += 2;
		else if (data[i] == '\n')
			len += 2;
		else if (data[i] == '\t')
			len += 2;
		else if (!isprint(data[i]))
			len += 6;
		else
			len += 1;
	}

	return len;
}

/* This function escape strings for the JSON format:
 * String is defined as:
 *
 *    any-Unicode-character-except-"-or-\-or-control-character
 *    \", \\, \/, \b, \f, \n, \r, \t, \u, four-hex-digits
 */
static inline
void json_escape_char_len(struct json *json, const char *data, size_t len)
{
	unsigned int i;

	for (i=0; i<len; i++) {
		/**/ if (data[i] == '"') {
			if (json->avalaible < 2) {
				json_close_all(json);
				return;
			}
			json->p[0] = '\\';
			json->p[1] = '"';
			json->avalaible -= 2;
			json->p += 2;
		}
		else if (data[i] == '\\') {
			if (json->avalaible < 2) {
				json_close_all(json);
				return;
			}
			json->p[0] = '\\';
			json->p[1] = '\\';
			json->avalaible -= 2;
			json->p += 2;
		}
		else if (data[i] == '/') {
			if (json->avalaible < 2) {
				json_close_all(json);
				return;
			}
			json->p[0] = '\\';
			json->p[1] = '/';
			json->avalaible -= 2;
			json->p += 2;
		}
		else if (data[i] == '\b') {
			if (json->avalaible < 2) {
				json_close_all(json);
				return;
			}
			json->p[0] = '\\';
			json->p[1] = 'b';
			json->avalaible -= 2;
			json->p += 2;
		}
		else if (data[i] == '\f') {
			if (json->avalaible < 2) {
				json_close_all(json);
				return;
			}
			json->p[0] = '\\';
			json->p[1] = 'f';
			json->avalaible -= 2;
			json->p += 2;
		}
		else if (data[i] == '\r') {
			if (json->avalaible < 2) {
				json_close_all(json);
				return;
			}
			json->p[0] = '\\';
			json->p[1] = 'r';
			json->avalaible -= 2;
			json->p += 2;
		}
		else if (data[i] == '\n') {
			if (json->avalaible < 2) {
				json_close_all(json);
				return;
			}
			json->p[0] = '\\';
			json->p[1] = 'n';
			json->avalaible -= 2;
			json->p += 2;
		}
		else if (data[i] == '\t') {
			if (json->avalaible < 2) {
				json_close_all(json);
				return;
			}
			json->p[0] = '\\';
			json->p[1] = 't';
			json->avalaible -= 2;
			json->p += 2;
		}
		else if (!isprint(data[i])) {
			if (json->avalaible < 6+1) { /* +1 for the finale \0 */
				json_close_all(json);
				return;
			}
			json->p[0] = '\\';
			json->p[1] = 'u';
			json->avalaible -= 2;
			json->p += 2;
			sprintf((char *)json->p, "%04x", data[i]);
			json->avalaible -= 4;
			json->p += 4;
		}
		else {
			if (json->avalaible < 1) {
				json_close_all(json);
				return;
			}
			json->p[0] = data[i];
			json->avalaible -= 1;
			json->p += 1;
		}
	}
}

void json_push(struct json *json, enum json_types elt, char *name, int len)
{
	int write_size = 0;
	int close_size = 0;
	int i;
	size_t indent = 0;

	/* check write avalaibility */
	if (!json->do_write)
		return;

	/* check stack size */
	if (json->level >= JSON_STACK_DEEP-1)
		return;

	/* check input element */
	switch (elt) {
	case JSON_ATTR:
	case JSON_OBJECT:
	case JSON_ARRAY:
	case JSON_STRING:
		break;
	case JSON_NUMBER:
	case JSON_TRUE:
	case JSON_FALSE:
	case JSON_NULL:
	case JSON_ANY:
		return;
	}

	/* the parent cannot be JSON_STRING, beacause JSON_STRING
	 * cannot have a children
	 */
	if (json->stack[json->level].elt == JSON_STRING)
		return;

	/*
	 *
	 * check avalaible size
	 *
	 */

	/* Compute indent */
	if (json->do_indent) {
		/* if the parent is JSON_ATTR, do no indent */
		if (json->stack[json->level].elt != JSON_ATTR) {
			for (i=0; i<=json->level; i++)
				if (json->stack[i].elt == JSON_OBJECT ||
				    json->stack[i].elt == JSON_ARRAY)
					indent++;
		}
	}

	/*
	 * open
	 */

	/* length of the separator with the last element */
	write_size +=  json_check_separator_len(json);

	/* if the element is JSON_ATTR, add the legnth of
	 * the escaped name
	 */
	if (elt == JSON_ATTR)
		write_size += 3 + json_escape_char_get_len(name, len); /* ", <name>, " and : */

	/* the opening tag */
	write_size ++; /* {, [, : or " */

	/*
	 * close
	 */

	/* the jumper, the tabulations.
	 * If the current tag is JSON_STRING or JSON_ATTR, dont
	 * indent.
	 */
	if (json->do_indent) {
		if (elt != JSON_STRING && elt != JSON_ATTR)
			close_size += json->do_indent + indent;
	}

	/* if object is JSON_ATTR, reserve space for "null" */
	if (elt == JSON_ATTR)
		close_size += strlen("null") - 1;

	/* the closing tag */
	close_size ++; /* }, ] or " */

	/* space avalaible ? */
	if (json->avalaible < write_size + close_size) {
		json_close_all(json);
		return;
	}

	/*
	 *
	 *
	 * Write open elements
	 *
	 *
	 */

	/* write separator with the last element */
	json_check_separator_write(json);

	/* the opening tag */
	switch (elt) {
	case JSON_OBJECT:
		json_add_char_noctl(json, '{');
		break;
	case JSON_ARRAY:
		json_add_char_noctl(json, '[');
		break;
	case JSON_STRING:
		json_add_char_noctl(json, '"');
		break;
	case JSON_ATTR:
		json_add_char_noctl(json, '"');
		json_escape_char_len(json, name, len);
		json_cat_pchar_noctl(json, "\":", 2);
		break;
	default:
		break;
	}

	/*
	 *
	 *
	 * Update stack and status
	 *
	 *
	 */
	json->stack[json->level].is_first = 0;
	json->level++;
	json->stack[json->level].is_first = 1;
	json->stack[json->level].reserved = close_size;
	json->stack[json->level].elt = elt;
	json_reserve(json, json->stack[json->level].reserved);

	return;
}

inline
void json_printf(struct json *json, const char *fmt, ...)
{
	va_list ap;
	int l;

	/* check write avalaibility */
	if (!json->do_write)
		return;

	/* only call into JSON_STRING level */
	if (json->stack[json->level].elt != JSON_STRING)
		return;

	va_start(ap, fmt);
	l = vsnprintf(json->p, json->avalaible, fmt, ap);
	va_end(ap);

	/* no enough space error. + 1 for the finale \0 */
	if (l == -1 || l + 1 > json->avalaible) {
		json_close_all(json);
		return;
	}

	json->p += l;
	json->avalaible -= l;
}

void json_cat_pchar(struct json *json, const char *cat, int len)
{
	/* check write avalaibility */
	if (!json->do_write)
		return;

	/* only call into JSON_STRING level */
	if (json->stack[json->level].elt != JSON_STRING)
		return;

	/* no enough space error */
	if (len > json->avalaible) {
		json_close_all(json);
		return;
	}

	memcpy(json->p, cat, len);
	json->p += len;
	json->avalaible -= len;
}

inline
void json_cat_escaped_pchar(struct json *json, const char *cat, int len)
{
	/* check write avalaibility */
	if (!json->do_write)
		return;

	/* only call into JSON_STRING level */
	if (json->stack[json->level].elt != JSON_STRING)
		return;

	/* no enough space error */
	if (json_escape_char_get_len(cat, len) > json->avalaible) {
		json_close_all(json);
		return;
	}

	json_escape_char_len(json, cat, len);
}

inline
void json_string_printf(struct json *json, const char *fmt, ...)
{
	va_list ap;
	int l;

	/* check write avalaibility */
	if (!json->do_write)
		return;

	json_push(json, JSON_STRING, NULL, 0);

	va_start(ap, fmt);
	l = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	/* no enough space error. + 1 for the finale \0 */
	if (l == -1 || l + 1 > json->avalaible) {
		json_close_all(json);
		return;
	}

	va_start(ap, fmt);
	l = vsnprintf(json->p, json->avalaible, fmt, ap);
	va_end(ap);

	json->p += l;
	json->avalaible -= l;

	json_pop(json);
}

void json_string_cat_pchar(struct json *json, const char *cat, int len)
{
	json_push(json, JSON_STRING, NULL, 0);
	json_cat_pchar(json, cat, len);
	json_pop(json);
}

void json_string_cat_escaped_pchar(struct json *json, const char *cat, int len)
{
	json_push(json, JSON_STRING, NULL, 0);
	json_cat_escaped_pchar(json, cat, len);
	json_pop(json);
}

inline
void json_number_cat_pchar(struct json *json, const char *cat, int len)
{
	/* check write avalaibility */
	if (!json->do_write)
		return;

	/* no enough space error */
	if (len + json_check_separator_len(json) > json->avalaible) {
		json_close_all(json);
		return;
	}
	json_check_separator_write(json);
	json_cat_pchar_noctl(json, cat, len);
	json->stack[json->level].is_first = 0;
}

inline
void json_number_printf(struct json *json, const char *fmt, ...)
{
	va_list ap;
	int l;

	/* check write avalaibility */
	if (!json->do_write)
		return;

	va_start(ap, fmt);
	l = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	/* no enough space error. + 1 for the final \0 */
	if (l == -1 || l + json_check_separator_len(json) + 1 > json->avalaible) {
		json_close_all(json);
		return;
	}

	json_check_separator_write(json);
	va_start(ap, fmt);
	vsnprintf(json->p, json->avalaible, fmt, ap);
	va_end(ap);

	json->p += l;
	json->avalaible -= l;
	json->stack[json->level].is_first = 0;
}

void json_number_int(struct json *json, int number)
{
	json_number_printf(json, "%d", number);
}

void json_number_double(struct json *json, double number)
{
	json_number_printf(json, "%lf", number);
}

void json_add_true(struct json *json)
{
	json_number_cat_pchar(json, "true", 4);
}

void json_add_false(struct json *json)
{
	json_number_cat_pchar(json, "false", 4);
}

void json_add_null(struct json *json)
{
	json_number_cat_pchar(json, "null", 4);
}

