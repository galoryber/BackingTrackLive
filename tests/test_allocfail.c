/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Systematic allocation-failure injection.
 *
 * Coverage reports put this project around 94% of lines but only ~69% of
 * branches, and nearly all of the missing branches are the same shape:
 * `if (!p) return BT_ERR_ALLOC;`. Nothing exercised them, so nobody knew
 * whether the cleanup on those paths was right - and a half-built object
 * abandoned on an error path leaks quietly, which is exactly the kind of bug
 * that shows up after an hour of a set rather than in a test.
 *
 * The method: fail the Nth allocation, run a workload, and require that it
 * neither crashes nor leaks. Then N+1, and so on, until N exceeds the number
 * of allocations the workload makes. Every error path gets walked in turn.
 *
 * Leak detection is done by counting live allocations rather than by LSan,
 * because LSan only reports at process exit and this needs a verdict per
 * iteration.
 *
 * Note this deliberately does not drive the vendored MP3 and FLAC decoders:
 * how dr_libs behaves out of memory is not this project's contract to keep.
 */
#include "bt_test.h"
#include "backtrack/bt_json.h"
#include "backtrack/bt_model.h"
#include "backtrack/bt_audio.h"
#include "backtrack/bt_wav.h"
#include "backtrack/bt_resample.h"
#include "backtrack/bt_engine.h"
#include "backtrack/bt_peaks.h"

extern void *__real_malloc(size_t);
extern void *__real_calloc(size_t, size_t);
extern void *__real_realloc(void *, size_t);
extern void  __real_free(void *);

static int  g_armed;       /* counting and failing are enabled          */
static long g_seen;        /* allocations attempted since arming        */
static long g_fail_at;     /* fail this one; 0 means never              */
static long g_live;        /* outstanding allocations                   */

void *__wrap_malloc(size_t n);
void *__wrap_calloc(size_t n, size_t m);
void *__wrap_realloc(void *p, size_t n);
void  __wrap_free(void *p);

static bool should_fail(void) {
    if (!g_armed) return false;
    g_seen++;
    return g_fail_at && g_seen == g_fail_at;
}

void *__wrap_malloc(size_t n) {
    if (should_fail()) return NULL;
    void *p = __real_malloc(n);
    if (p && g_armed) g_live++;
    return p;
}

void *__wrap_calloc(size_t n, size_t m) {
    if (should_fail()) return NULL;
    void *p = __real_calloc(n, m);
    if (p && g_armed) g_live++;
    return p;
}

void *__wrap_realloc(void *p, size_t n) {
    if (should_fail()) return NULL;          /* the original stays valid */
    void *q = __real_realloc(p, n);
    if (!g_armed) return q;
    if (!p && q)      g_live++;              /* behaved as malloc        */
    else if (p && !n) g_live--;              /* behaved as free          */
    return q;
}

void __wrap_free(void *p) {
    if (p && g_armed) g_live--;
    __real_free(p);
}

/* ---------------------------------------------------------- workloads */

static const char *SETLIST_JSON =
"{\"version\":1,\"name\":\"Alloc\",\"songs\":["
"{\"title\":\"One\",\"artist\":\"A\",\"tempo\":{\"map\":["
"{\"beat\":0,\"bpm\":120},{\"beat\":16,\"bpm\":96}],\"sig\":[4,4],"
"\"downbeat_ms\":12.5},\"count_in_bars\":2,\"on_end\":\"next\",\"tracks\":["
"{\"name\":\"Click\",\"type\":\"click\",\"bus\":\"inear\"},"
"{\"name\":\"Synth\",\"type\":\"audio\",\"bus\":\"foh\",\"file\":\"a.wav\"},"
"{\"name\":\"Bass\",\"type\":\"audio\",\"bus\":\"foh\",\"file\":\"b.wav\"}]},"
"{\"title\":\"Two\",\"tempo\":{\"bpm\":100},\"tracks\":["
"{\"type\":\"click\",\"bus\":\"inear\"}]}]}";

static void work_json(void) {
    bt_json *j = NULL;
    int line = 0;
    if (bt_json_parse(SETLIST_JSON, strlen(SETLIST_JSON), &j, &line) == BT_OK) {
        (void)bt_json_len(bt_json_get(j, "songs"));
        bt_json_free(j);
    }
}

static void work_setlist(void) {
    bt_setlist *sl = NULL;
    int line = 0;
    if (bt_setlist_load_mem(SETLIST_JSON, strlen(SETLIST_JSON), "", &sl, &line) == BT_OK)
        bt_setlist_free(sl);
}

/* Deliberately long enough that the serialiser's buffer has to grow several
 * times: one allocation would leave the realloc path untested, and growth is
 * where a builder abandons its old buffer. */
static char *big_setlist_json(void) {
    size_t cap = 1 << 16;
    char  *j   = (char *)__real_malloc(cap);
    size_t o   = 0;
    o += (size_t)snprintf(j + o, cap - o, "{\"version\":1,\"name\":\"Big\",\"songs\":[");
    for (int i = 0; i < 40; i++)
        o += (size_t)snprintf(j + o, cap - o,
            "%s{\"title\":\"Song number %d with a deliberately long title\","
            "\"artist\":\"Some Artist Name\",\"tempo\":{\"bpm\":%d.37},"
            "\"tracks\":[{\"type\":\"click\",\"bus\":\"inear\"},"
            "{\"name\":\"Track\",\"type\":\"audio\",\"bus\":\"foh\","
            "\"file\":\"stems/song%d/track.wav\"}]}",
            i ? "," : "", i, 90 + i, i);
    snprintf(j + o, cap - o, "]}");
    return j;
}

static void work_setlist_write(void) {
    /* Build without injection, then serialise with it: the interesting path
     * is the string builder growing, not the parse. */
    bt_setlist *sl = NULL;
    int line = 0;
    int saved = g_armed;
    g_armed = 0;
    char *json = big_setlist_json();
    bt_err e = bt_setlist_load_mem(json, strlen(json), "", &sl, &line);
    __real_free(json);
    g_armed = saved;
    if (e != BT_OK) return;

    char  *out = NULL;
    size_t len = 0;
    if (bt_setlist_to_json(sl, &out, &len) == BT_OK) free(out);

    g_armed = 0;
    bt_setlist_free(sl);
    g_armed = saved;
}

static unsigned char *g_wav;
static size_t         g_wav_len;

static void work_wav_decode(void) {
    bt_audio a;
    if (bt_audio_decode_mem(g_wav, g_wav_len, "wav", &a) == BT_OK)
        bt_audio_free(&a);
}

static float *g_src;
static const bt_frame SRC_FRAMES = 4000;

static void work_resample(void) {
    const float *in[2] = { g_src, g_src };
    float **out = NULL;
    bt_frame n = 0;
    if (bt_resample_planar(in, 2, SRC_FRAMES, 44100, 48000, &out, &n) == BT_OK) {
        for (int c = 0; c < 2; c++) free(out[c]);
        free(out);
    }
}

static void work_peaks(void) {
    const float *in[1] = { g_src };
    bt_peaks p;
    if (bt_peaks_build(in, 1, SRC_FRAMES, 64, &p) == BT_OK) bt_peaks_free(&p);
}

static void work_engine(void) {
    bt_engine_cfg c = { 48000, 4, 512 };
    bt_engine *e = NULL;
    if (bt_engine_create(&c, &e) == BT_OK) {
        bt_click_voice v;
        bt_click_voice_defaults(&v);
        (void)bt_engine_set_click_voice(e, &v);
        bt_engine_destroy(e);
    }
}

/* ------------------------------------------------------------- driver */

typedef void (*workload)(void);

/* Runs `w` once with no failure injected, to learn how many allocations it
 * makes, then once per allocation with that one failing. */
static void sweep(workload w, const char *name) {
    g_fail_at = 0;
    g_live    = 0;
    g_seen    = 0;
    g_armed   = 1;
    w();
    g_armed   = 0;

    long total = g_seen;
    bt_checks++;
    if (g_live != 0) {
        bt_fails++;
        fprintf(stderr, "  FAIL %s leaks %ld allocation(s) even when nothing fails\n",
                name, g_live);
        return;
    }
    if (total == 0) {
        fprintf(stderr, "  (%s makes no allocations)\n", name);
        return;
    }

    long leaked_at = -1, leaked_n = 0;
    for (long n = 1; n <= total; n++) {
        g_fail_at = n;
        g_live    = 0;
        g_seen    = 0;
        g_armed   = 1;
        w();                       /* must not crash */
        g_armed   = 0;
        if (g_live != 0 && leaked_at < 0) { leaked_at = n; leaked_n = g_live; }
    }
    g_fail_at = 0;

    bt_checks++;
    if (leaked_at >= 0) {
        bt_fails++;
        fprintf(stderr, "  FAIL %s leaks %ld allocation(s) when allocation #%ld "
                        "of %ld fails\n", name, leaked_n, leaked_at, total);
    } else {
        fprintf(stderr, "  %s: %ld allocation(s), every failure point clean\n",
                name, total);
    }
}

static void build_fixtures(void) {
    /* A small WAV in memory, built without injection. */
    const bt_frame n = 500;
    float *buf = (float *)__real_malloc((size_t)n * sizeof(float));
    bt_lcg_seed(3);
    for (bt_frame i = 0; i < n; i++) buf[i] = bt_lcg_sample();
    const float *p[1] = { buf };
    BT_CHECK_EQI(bt_wav_write_file("allocfail.wav", p, 1, 44100, n), BT_OK);
    __real_free(buf);

    FILE *f = fopen("allocfail.wav", "rb");
    BT_CHECK(f != NULL);
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        rewind(f);
        g_wav = (unsigned char *)__real_malloc((size_t)sz);
        g_wav_len = fread(g_wav, 1, (size_t)sz, f);
        fclose(f);
    }
    remove("allocfail.wav");

    g_src = (float *)__real_malloc((size_t)SRC_FRAMES * sizeof(float));
    bt_lcg_seed(9);
    for (bt_frame i = 0; i < SRC_FRAMES; i++) g_src[i] = bt_lcg_sample();
}

static void test_every_allocation_failure(void) {
    build_fixtures();
    sweep(work_json,           "bt_json_parse");
    sweep(work_setlist,        "bt_setlist_load_mem");
    sweep(work_setlist_write,  "bt_setlist_to_json");
    sweep(work_wav_decode,     "bt_wav_decode");
    sweep(work_resample,       "bt_resample_planar");
    sweep(work_peaks,          "bt_peaks_build");
    sweep(work_engine,         "bt_engine_create");
    __real_free(g_wav);
    __real_free(g_src);
}

int main(void) {
    BT_RUN(test_every_allocation_failure);
    BT_REPORT();
}
