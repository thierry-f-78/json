/*
 * Copyright 2013-2019 Thierry FOURNIER
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License.
 *
 */
#ifndef __JSON_H__
#define __JSON_H__

#define JSON_STACK_DEEP 20

enum json_types {
	JSON_ANY = 0,
	JSON_STRING,
	JSON_NUMBER,
	JSON_OBJECT,
	JSON_ATTR,
	JSON_ARRAY,
	JSON_TRUE,
	JSON_FALSE,
	JSON_NULL,
};

struct json_stack {
	int is_first;
	int reserved;
	enum json_types elt;
};

/* This struct permit to fill the buffer with fprintf-like méthod */
struct json {
	char *buffer;
	char *p;
	int len;
	int avalaible;

	int do_write;
	int do_indent;

	int level;
	struct json_stack stack[JSON_STACK_DEEP];
};

/* initialize the struct json
 * "json" is preallocated struct
 * "buffer" is the write buffer
 * "len" is the len of the buffer
 * "do_indent" is the enbale indentation flag
 */
void json_init(struct json *json, char *buffer, int len, int do_indent);

/* push new json level. The avalaible element are
 *  JSON_ARRAY  -> json array  [...]
 *  JSON_OBJECT -> json object {...}
 *  JSON_ATTR   -> object attribute. The parent must be an object.
 *                 "...":<any>. The pop function set "null" is no one
 *                 object is set. This level take the argument "name"
 *                 and his len. The name is escaped.
 *  JSON_STRING -> A string "..."
 */
void json_push(struct json *json, enum json_types elt, char *name, int len);
static inline
void json_push_string(struct json *json, enum json_types elt, char *name)
{
	json_push(json, elt, name, strlen(name));
}

/* close the last pushed element */
void json_pop(struct json *json);

/* finalize the json message */
void json_finalize(struct json *json);

/*
 * the following functions can be avalaible only into JSON_STRING level
 */

/* printf like formatter */
void json_printf(struct json *json, const char *fmt, ...)
                 __attribute__((format(printf, 2, 3)));

/* catenate buffers */
void json_cat_pchar(struct json *json, const char *cat, int len);
static inline
void json_cat_string(struct json *json, const char *cat)
{
	json_cat_pchar(json, cat, strlen(cat));
}

/* escape buffer and catenate */
void json_cat_escaped_pchar(struct json *json, const char *cat, int len);

/*
 * The following functions include the push and pop of JSON_STRING
 */

/* printf like formatter */
void json_string_printf(struct json *json, const char *fmt, ...)
                        __attribute__((format(printf, 2, 3)));

/* catenate buffers */
void json_string_cat_pchar(struct json *json, const char *cat, int len);
static inline
void json_string_cat_string(struct json *json, const char *cat)
{
	json_string_cat_pchar(json, cat, strlen(cat));
}

/* escape buffer and catenate */
void json_string_cat_escaped_pchar(struct json *json, const char *cat, int len);

/*
 * The following function can be used for printing integer
 */

/* already string formatted integer */
void json_number_cat_pchar(struct json *json, const char *cat, int len);

/* user printf like formatter */
void json_number_printf(struct json *json, const char *fmt, ...)
                        __attribute__((format(printf, 2, 3)));

/* format simple integer */
void json_number_int(struct json *json, int number);

/* format simple double */
void json_number_double(struct json *json, double number);

/*
 * Add simple language words
 */
void json_add_true(struct json *json);
void json_add_false(struct json *json);
void json_add_null(struct json *json);

#endif /* __JSON_H__ */
