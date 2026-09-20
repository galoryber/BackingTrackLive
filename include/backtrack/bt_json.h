/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Minimal, strict, allocation-bounded JSON parser.
 *
 * Deliberately hand-rolled rather than vendored: this is the primary untrusted
 * input surface of the application, so it is small enough to audit and is
 * covered by a libFuzzer target (fuzz/fuzz_json.c).
 */
#ifndef BT_JSON_H
#define BT_JSON_H

#include "bt_types.h"
#include "bt_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BT_JSON_NULL = 0,
    BT_JSON_BOOL,
    BT_JSON_NUMBER,
    BT_JSON_STRING,
    BT_JSON_ARRAY,
    BT_JSON_OBJECT
} bt_json_type;

typedef struct bt_json bt_json;

/* Parse a NUL-terminated buffer. On failure returns non-OK and, if `line` is
 * non-NULL, writes the 1-based line of the offending token. */
bt_err      bt_json_parse(const char *text, size_t len, bt_json **out, int *line);
void        bt_json_free(bt_json *j);

bt_json_type bt_json_typeof(const bt_json *j);

/* Object access. Returns NULL when absent or when `j` is not an object. */
const bt_json *bt_json_get(const bt_json *j, const char *key);

/* Array access. */
size_t         bt_json_len(const bt_json *j);
const bt_json *bt_json_at(const bt_json *j, size_t i);

/* Scalar access. Each returns the supplied default when the node is NULL or of
 * the wrong type, which keeps schema binding free of NULL-checking noise. */
double      bt_json_number(const bt_json *j, double dflt);
bool        bt_json_bool(const bt_json *j, bool dflt);
const char *bt_json_string(const bt_json *j, const char *dflt);

#ifdef __cplusplus
}
#endif
#endif /* BT_JSON_H */
