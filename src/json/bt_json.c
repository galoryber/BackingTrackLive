/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Strict recursive-descent JSON parser.
 *
 * Strict means: no trailing commas, no comments, no NaN/Infinity literals, no
 * unquoted keys, no control characters inside strings. A set list is a file we
 * generate and a human occasionally edits; being permissive here buys nothing
 * and widens the surface that fuzz/fuzz_json.c has to defend.
 */
#include "backtrack/bt_json.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define BT_JSON_MAX_DEPTH 64

struct bt_json {
    bt_json_type type;
    union {
        bool   b;
        double num;
        char  *str;
        struct { bt_json **items; size_t n, cap; } arr;
        struct { char **keys; bt_json **vals; size_t n, cap; } obj;
    } u;
};

typedef struct {
    const char *p;
    const char *end;
    int         line;
    int         depth;
} bt_scan;

static bt_err parse_value(bt_scan *s, bt_json **out);

/* ------------------------------------------------------------------ utils */

static bt_json *node_new(bt_json_type t) {
    bt_json *j = (bt_json *)calloc(1, sizeof(*j));
    if (j) j->type = t;
    return j;
}

static void skip_ws(bt_scan *s) {
    while (s->p < s->end) {
        char c = *s->p;
        if (c == '\n') { s->line++; s->p++; }
        else if (c == ' ' || c == '\t' || c == '\r') s->p++;
        else break;
    }
}

static bool eat(bt_scan *s, char c) {
    skip_ws(s);
    if (s->p < s->end && *s->p == c) { s->p++; return true; }
    return false;
}

/* ----------------------------------------------------------------- string */

static int hex4(const char *p) {
    int v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return -1;
        v = (v << 4) | d;
    }
    return v;
}

static size_t utf8_put(char *dst, unsigned cp) {
    if (cp < 0x80)    { dst[0] = (char)cp; return 1; }
    if (cp < 0x800)   { dst[0] = (char)(0xC0 | (cp >> 6));
                        dst[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) { dst[0] = (char)(0xE0 | (cp >> 12));
                        dst[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        dst[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
    dst[0] = (char)(0xF0 | (cp >> 18));
    dst[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    dst[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    dst[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* Parses a string literal. The opening quote must already be consumed.
 * The decoded form is never longer than the source, so one allocation sized
 * to the remaining input is always sufficient. */
static bt_err parse_string_raw(bt_scan *s, char **out) {
    size_t cap = (size_t)(s->end - s->p) + 1;
    char  *buf = (char *)malloc(cap);
    if (!buf) return BT_ERR_ALLOC;
    size_t n = 0;

    while (s->p < s->end) {
        unsigned char c = (unsigned char)*s->p;

        if (c == '"') { s->p++; buf[n] = '\0'; *out = buf; return BT_OK; }

        if (c < 0x20) { free(buf); return BT_ERR_PARSE; }  /* raw control */

        if (c != '\\') { buf[n++] = (char)c; s->p++; continue; }

        s->p++;
        if (s->p >= s->end) { free(buf); return BT_ERR_PARSE; }
        char esc = *s->p++;
        switch (esc) {
        case '"':  buf[n++] = '"';  break;
        case '\\': buf[n++] = '\\'; break;
        case '/':  buf[n++] = '/';  break;
        case 'b':  buf[n++] = '\b'; break;
        case 'f':  buf[n++] = '\f'; break;
        case 'n':  buf[n++] = '\n'; break;
        case 'r':  buf[n++] = '\r'; break;
        case 't':  buf[n++] = '\t'; break;
        case 'u': {
            if (s->end - s->p < 4) { free(buf); return BT_ERR_PARSE; }
            int hi = hex4(s->p);
            if (hi < 0) { free(buf); return BT_ERR_PARSE; }
            s->p += 4;
            unsigned cp = (unsigned)hi;
            if (cp >= 0xD800 && cp <= 0xDBFF) {          /* high surrogate */
                if (s->end - s->p < 6 || s->p[0] != '\\' || s->p[1] != 'u') {
                    free(buf); return BT_ERR_PARSE;
                }
                int lo = hex4(s->p + 2);
                if (lo < 0xDC00 || lo > 0xDFFF) { free(buf); return BT_ERR_PARSE; }
                s->p += 6;
                cp = 0x10000u + ((cp - 0xD800u) << 10) + ((unsigned)lo - 0xDC00u);
            } else if (cp >= 0xDC00 && cp <= 0xDFFF) {   /* lone low */
                free(buf); return BT_ERR_PARSE;
            }
            n += utf8_put(buf + n, cp);
            break;
        }
        default: free(buf); return BT_ERR_PARSE;
        }
    }
    free(buf);
    return BT_ERR_PARSE;   /* unterminated */
}

/* ----------------------------------------------------------- collections */

static bt_err arr_push(bt_json *j, bt_json *v) {
    if (j->u.arr.n == j->u.arr.cap) {
        size_t cap = j->u.arr.cap ? j->u.arr.cap * 2 : 8;
        bt_json **q = (bt_json **)realloc(j->u.arr.items, cap * sizeof(*q));
        if (!q) return BT_ERR_ALLOC;
        j->u.arr.items = q;
        j->u.arr.cap   = cap;
    }
    j->u.arr.items[j->u.arr.n++] = v;
    return BT_OK;
}

static bt_err obj_push(bt_json *j, char *k, bt_json *v) {
    if (j->u.obj.n == j->u.obj.cap) {
        size_t cap = j->u.obj.cap ? j->u.obj.cap * 2 : 8;
        char    **nk = (char **)realloc(j->u.obj.keys, cap * sizeof(*nk));
        if (!nk) return BT_ERR_ALLOC;
        j->u.obj.keys = nk;
        bt_json **nv = (bt_json **)realloc(j->u.obj.vals, cap * sizeof(*nv));
        if (!nv) return BT_ERR_ALLOC;
        j->u.obj.vals = nv;
        j->u.obj.cap  = cap;
    }
    j->u.obj.keys[j->u.obj.n] = k;
    j->u.obj.vals[j->u.obj.n] = v;
    j->u.obj.n++;
    return BT_OK;
}

static bt_err parse_array(bt_scan *s, bt_json **out) {
    bt_json *j = node_new(BT_JSON_ARRAY);
    if (!j) return BT_ERR_ALLOC;

    if (eat(s, ']')) { *out = j; return BT_OK; }

    for (;;) {
        bt_json *v = NULL;
        bt_err e = parse_value(s, &v);
        if (e != BT_OK) { bt_json_free(j); return e; }
        if ((e = arr_push(j, v)) != BT_OK) {
            bt_json_free(v); bt_json_free(j); return e;
        }
        if (eat(s, ',')) continue;
        if (eat(s, ']')) { *out = j; return BT_OK; }
        bt_json_free(j);
        return BT_ERR_PARSE;
    }
}

static bt_err parse_object(bt_scan *s, bt_json **out) {
    bt_json *j = node_new(BT_JSON_OBJECT);
    if (!j) return BT_ERR_ALLOC;

    if (eat(s, '}')) { *out = j; return BT_OK; }

    for (;;) {
        if (!eat(s, '"')) { bt_json_free(j); return BT_ERR_PARSE; }
        char *key = NULL;
        bt_err e = parse_string_raw(s, &key);
        if (e != BT_OK) { bt_json_free(j); return e; }

        if (!eat(s, ':')) { free(key); bt_json_free(j); return BT_ERR_PARSE; }

        bt_json *v = NULL;
        if ((e = parse_value(s, &v)) != BT_OK) {
            free(key); bt_json_free(j); return e;
        }
        if ((e = obj_push(j, key, v)) != BT_OK) {
            free(key); bt_json_free(v); bt_json_free(j); return e;
        }
        if (eat(s, ',')) continue;
        if (eat(s, '}')) { *out = j; return BT_OK; }
        bt_json_free(j);
        return BT_ERR_PARSE;
    }
}

/* ------------------------------------------------------------------ value */

static bool lit(bt_scan *s, const char *word) {
    size_t n = strlen(word);
    if ((size_t)(s->end - s->p) < n) return false;
    if (memcmp(s->p, word, n) != 0) return false;
    s->p += n;
    return true;
}

static bt_err parse_number(bt_scan *s, bt_json **out) {
    /* Validate the grammar ourselves, then hand the span to strtod. This
     * rejects the forms strtod would happily accept but JSON does not:
     * "0x10", "1.", ".5", "+1", "Infinity", "nan". */
    const char *b = s->p;
    if (s->p < s->end && *s->p == '-') s->p++;

    if (s->p >= s->end) return BT_ERR_PARSE;
    if (*s->p == '0') {
        s->p++;
    } else if (*s->p >= '1' && *s->p <= '9') {
        while (s->p < s->end && *s->p >= '0' && *s->p <= '9') s->p++;
    } else {
        return BT_ERR_PARSE;
    }

    if (s->p < s->end && *s->p == '.') {
        s->p++;
        if (s->p >= s->end || *s->p < '0' || *s->p > '9') return BT_ERR_PARSE;
        while (s->p < s->end && *s->p >= '0' && *s->p <= '9') s->p++;
    }

    if (s->p < s->end && (*s->p == 'e' || *s->p == 'E')) {
        s->p++;
        if (s->p < s->end && (*s->p == '+' || *s->p == '-')) s->p++;
        if (s->p >= s->end || *s->p < '0' || *s->p > '9') return BT_ERR_PARSE;
        while (s->p < s->end && *s->p >= '0' && *s->p <= '9') s->p++;
    }

    char tmp[64];
    size_t n = (size_t)(s->p - b);
    if (n >= sizeof(tmp)) return BT_ERR_PARSE;
    memcpy(tmp, b, n);
    tmp[n] = '\0';

    bt_json *j = node_new(BT_JSON_NUMBER);
    if (!j) return BT_ERR_ALLOC;
    j->u.num = strtod(tmp, NULL);
    *out = j;
    return BT_OK;
}

static bt_err parse_value(bt_scan *s, bt_json **out) {
    if (++s->depth > BT_JSON_MAX_DEPTH) { s->depth--; return BT_ERR_PARSE; }
    skip_ws(s);
    bt_err e = BT_ERR_PARSE;

    if (s->p >= s->end) { s->depth--; return BT_ERR_PARSE; }

    char c = *s->p;
    if (c == '{') { s->p++; e = parse_object(s, out); }
    else if (c == '[') { s->p++; e = parse_array(s, out); }
    else if (c == '"') {
        s->p++;
        char *str = NULL;
        e = parse_string_raw(s, &str);
        if (e == BT_OK) {
            bt_json *j = node_new(BT_JSON_STRING);
            if (!j) { free(str); e = BT_ERR_ALLOC; }
            else { j->u.str = str; *out = j; }
        }
    }
    else if (c == 't' && lit(s, "true"))  { bt_json *j = node_new(BT_JSON_BOOL);
                                            if (!j) e = BT_ERR_ALLOC;
                                            else { j->u.b = true;  *out = j; e = BT_OK; } }
    else if (c == 'f' && lit(s, "false")) { bt_json *j = node_new(BT_JSON_BOOL);
                                            if (!j) e = BT_ERR_ALLOC;
                                            else { j->u.b = false; *out = j; e = BT_OK; } }
    else if (c == 'n' && lit(s, "null"))  { bt_json *j = node_new(BT_JSON_NULL);
                                            if (!j) e = BT_ERR_ALLOC;
                                            else { *out = j; e = BT_OK; } }
    else if (c == '-' || (c >= '0' && c <= '9')) { e = parse_number(s, out); }

    s->depth--;
    return e;
}

/* -------------------------------------------------------------- public API */

bt_err bt_json_parse(const char *text, size_t len, bt_json **out, int *line) {
    if (!text || !out) return BT_ERR_RANGE;
    *out = NULL;

    bt_scan s = { text, text + len, 1, 0 };

    /* Tolerate a UTF-8 BOM: editors on Windows add it without being asked. */
    if (len >= 3 && (unsigned char)s.p[0] == 0xEF
                 && (unsigned char)s.p[1] == 0xBB
                 && (unsigned char)s.p[2] == 0xBF) s.p += 3;

    bt_json *root = NULL;
    bt_err e = parse_value(&s, &root);
    if (e != BT_OK) { if (line) *line = s.line; return e; }

    skip_ws(&s);
    if (s.p != s.end) {                       /* trailing garbage */
        bt_json_free(root);
        if (line) *line = s.line;
        return BT_ERR_PARSE;
    }
    *out = root;
    return BT_OK;
}

void bt_json_free(bt_json *j) {
    if (!j) return;
    switch (j->type) {
    case BT_JSON_STRING:
        free(j->u.str);
        break;
    case BT_JSON_ARRAY:
        for (size_t i = 0; i < j->u.arr.n; i++) bt_json_free(j->u.arr.items[i]);
        free(j->u.arr.items);
        break;
    case BT_JSON_OBJECT:
        for (size_t i = 0; i < j->u.obj.n; i++) {
            free(j->u.obj.keys[i]);
            bt_json_free(j->u.obj.vals[i]);
        }
        free(j->u.obj.keys);
        free(j->u.obj.vals);
        break;
    default:
        break;
    }
    free(j);
}

bt_json_type bt_json_typeof(const bt_json *j) {
    return j ? j->type : BT_JSON_NULL;
}

const bt_json *bt_json_get(const bt_json *j, const char *key) {
    if (!j || j->type != BT_JSON_OBJECT || !key) return NULL;
    for (size_t i = 0; i < j->u.obj.n; i++)
        if (strcmp(j->u.obj.keys[i], key) == 0) return j->u.obj.vals[i];
    return NULL;
}

size_t bt_json_len(const bt_json *j) {
    if (!j) return 0;
    if (j->type == BT_JSON_ARRAY)  return j->u.arr.n;
    if (j->type == BT_JSON_OBJECT) return j->u.obj.n;
    return 0;
}

const bt_json *bt_json_at(const bt_json *j, size_t i) {
    if (!j || j->type != BT_JSON_ARRAY || i >= j->u.arr.n) return NULL;
    return j->u.arr.items[i];
}

double bt_json_number(const bt_json *j, double dflt) {
    return (j && j->type == BT_JSON_NUMBER) ? j->u.num : dflt;
}

bool bt_json_bool(const bt_json *j, bool dflt) {
    return (j && j->type == BT_JSON_BOOL) ? j->u.b : dflt;
}

const char *bt_json_string(const bt_json *j, const char *dflt) {
    return (j && j->type == BT_JSON_STRING) ? j->u.str : dflt;
}
