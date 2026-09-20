/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 * A deliberately tiny test harness: no framework, no discovery, no magic. */
#ifndef BT_TEST_H
#define BT_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

static int bt_checks = 0;
static int bt_fails  = 0;

#define BT_CHECK(cond)                                                        \
    do {                                                                      \
        bt_checks++;                                                          \
        if (!(cond)) {                                                        \
            bt_fails++;                                                       \
            fprintf(stderr, "  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                                     \
    } while (0)

#define BT_CHECK_EQI(a, b)                                                    \
    do {                                                                      \
        bt_checks++;                                                          \
        long long _a = (long long)(a), _b = (long long)(b);                   \
        if (_a != _b) {                                                       \
            bt_fails++;                                                       \
            fprintf(stderr, "  FAIL %s:%d  %s == %s  (%lld vs %lld)\n",       \
                    __FILE__, __LINE__, #a, #b, _a, _b);                      \
        }                                                                     \
    } while (0)

#define BT_CHECK_NEAR(a, b, eps)                                              \
    do {                                                                      \
        bt_checks++;                                                          \
        double _a = (double)(a), _b = (double)(b);                            \
        if (fabs(_a - _b) > (eps)) {                                          \
            bt_fails++;                                                       \
            fprintf(stderr, "  FAIL %s:%d  %s ~= %s  (%g vs %g)\n",           \
                    __FILE__, __LINE__, #a, #b, _a, _b);                      \
        }                                                                     \
    } while (0)

#define BT_RUN(fn)                                                            \
    do {                                                                      \
        fprintf(stderr, "-- %s\n", #fn);                                      \
        fn();                                                                 \
    } while (0)

#define BT_REPORT()                                                           \
    do {                                                                      \
        fprintf(stderr, "%s: %d checks, %d failures\n",                       \
                bt_fails ? "FAILED" : "ok", bt_checks, bt_fails);             \
        return bt_fails ? 1 : 0;                                              \
    } while (0)

/* Deterministic fixture audio. No libm, so the samples are bit-identical on
 * every platform - which is what makes the golden render test portable. */
static uint32_t bt_lcg_state = 1u;

static inline void bt_lcg_seed(uint32_t s) { bt_lcg_state = s ? s : 1u; }

static inline float bt_lcg_sample(void) {
    bt_lcg_state = (uint32_t)(bt_lcg_state * 1664525u + 1013904223u);
    int32_t v = (int32_t)((bt_lcg_state >> 8) % 2001u) - 1000;
    return (float)v / 1000.0f;
}

/* FNV-1a over the raw bytes of the rendered block. */
static inline uint64_t bt_hash_init(void) { return 1469598103934665603ULL; }

static inline uint64_t bt_hash_bytes(uint64_t h, const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) {
        h ^= (uint64_t)b[i];
        h *= 1099511628211ULL;
    }
    return h;
}

#endif /* BT_TEST_H */
