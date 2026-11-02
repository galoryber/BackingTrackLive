/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Internal: the growable buffer and the atomic file write shared by the two
 * things that produce JSON - the set list writer and the lighting cue file
 * writer. Not part of the public API.
 */
#ifndef BT_WRITE_UTIL_H
#define BT_WRITE_UTIL_H

#include "backtrack/bt_error.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char  *buf;
    size_t len, cap;
    bool   failed;      /* an allocation failed; the result is discarded */
} sb;

void   bt__sb_raw(sb *s, const char *text, size_t n);
void   bt__sb_str(sb *s, const char *text);
void   bt__sb_fmt(sb *s, const char *fmt, ...);
void   bt__sb_json_string(sb *s, const char *v);

/* Writes to a temporary alongside the target and renames over it, so an
 * interrupted save cannot leave a half-written file where a working one was. */
bt_err bt__write_atomic(const char *path, const char *data, size_t len);

#endif /* BT_WRITE_UTIL_H */
