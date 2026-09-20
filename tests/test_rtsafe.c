/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Real-time safety, enforced mechanically.
 *
 * The single most common cause of a playback rig glitching on stage is an
 * allocation - or a lock, or a log line - that crept into the audio callback.
 * This test wraps the allocator at link time and asserts that a render
 * performs none. It is cheap to run and it fails loudly the day somebody adds
 * a malloc() to the mixer.
 */
#include "bt_test.h"
#include "backtrack/bt_engine.h"

#define SR 48000

extern void *__real_malloc(size_t);
extern void *__real_calloc(size_t, size_t);
extern void *__real_realloc(void *, size_t);
extern void  __real_free(void *);

static volatile long g_allocs = 0;
static volatile int  g_watch  = 0;

void *__wrap_malloc(size_t n);
void *__wrap_calloc(size_t n, size_t m);
void *__wrap_realloc(void *p, size_t n);
void  __wrap_free(void *p);

void *__wrap_malloc(size_t n) {
    if (g_watch) g_allocs++;
    return __real_malloc(n);
}
void *__wrap_calloc(size_t n, size_t m) {
    if (g_watch) g_allocs++;
    return __real_calloc(n, m);
}
void *__wrap_realloc(void *p, size_t n) {
    if (g_watch) g_allocs++;
    return __real_realloc(p, n);
}
void __wrap_free(void *p) {
    if (g_watch && p) g_allocs++;
    __real_free(p);
}

static bt_track mk_click(const char *bus) {
    bt_track t;
    memset(&t, 0, sizeof(t));
    t.type = BT_TRACK_CLICK;
    snprintf(t.bus, sizeof(t.bus), "%s", bus);
    return t;
}

static bt_track mk_audio(const char *bus, int32_t ch, bt_frame n, int32_t off_ms) {
    bt_track t;
    memset(&t, 0, sizeof(t));
    t.type = BT_TRACK_AUDIO;
    snprintf(t.bus, sizeof(t.bus), "%s", bus);
    snprintf(t.file, sizeof(t.file), "x.wav");
    t.offset_ms = off_ms;
    t.channels = ch;
    t.frames = n;
    t.pcm = (float **)calloc((size_t)ch, sizeof(float *));
    for (int32_t c = 0; c < ch; c++) {
        t.pcm[c] = (float *)calloc((size_t)n, sizeof(float));
        for (bt_frame i = 0; i < n; i++) t.pcm[c][i] = 0.1f;
    }
    return t;
}

static void test_render_allocates_nothing(void) {
    bt_song s;
    memset(&s, 0, sizeof(s));
    s.tempo.seg[0].bpm = 143.7;       /* awkward on purpose */
    s.tempo.nseg = 1;
    s.tempo.sig_num = 4;
    s.tempo.sig_den = 4;
    s.count_in_bars = 2;

    s.track[0] = mk_click("inear");
    for (int i = 1; i < 9; i++)
        s.track[i] = mk_audio((i % 2) ? "foh" : "inear", (i % 3) ? 1 : 2,
                              SR * 2, (int32_t)(i * 3 - 12));
    s.ntracks = 9;

    bt_device_cfg d;
    memset(&d, 0, sizeof(d));
    d.sample_rate = SR;
    snprintf(d.bus[0].name, BT_MAX_NAME, "foh");
    d.bus[0].ch[0] = 0; d.bus[0].ch[1] = 1; d.bus[0].nch = 2;
    snprintf(d.bus[1].name, BT_MAX_NAME, "inear");
    d.bus[1].ch[0] = 2; d.bus[1].ch[1] = 3; d.bus[1].nch = 2;
    d.nbuses = 2;

    bt_engine_cfg ec = { SR, 4, 1024 };
    bt_engine *e = NULL;
    BT_CHECK_EQI(bt_engine_create(&ec, &e), BT_OK);
    BT_CHECK_EQI(bt_engine_set_song(e, &s, &d), BT_OK);
    bt_engine_start_with_count_in(e);

    float *buf[4];
    float *win[4];
    for (int c = 0; c < 4; c++) buf[c] = (float *)calloc(1024, sizeof(float));
    for (int c = 0; c < 4; c++) win[c] = buf[c];

    /* Warm up outside the watch, then measure. Nothing between the two
     * assignments may allocate - including anything this test itself does. */
    bt_engine_render(e, win, 1024);

    g_allocs = 0;
    g_watch  = 1;
    for (int i = 0; i < 400; i++) bt_engine_render(e, win, 1024);
    g_watch = 0;

    BT_CHECK_EQI(g_allocs, 0);

    /* Transport calls are on the RT path too (a footswitch hits them). */
    g_allocs = 0;
    g_watch  = 1;
    for (int i = 0; i < 100; i++) {
        bt_engine_seek(e, (bt_frame)i * 977);
        bt_engine_play(e);
        bt_engine_render(e, win, 256);
        bt_engine_stop(e);
        (void)bt_engine_playhead(e);
        (void)bt_engine_finished(e);
    }
    g_watch = 0;
    BT_CHECK_EQI(g_allocs, 0);

    for (int c = 0; c < 4; c++) free(buf[c]);
    bt_engine_destroy(e);
    bt_song_free_audio(&s);
}

int main(void) {
    BT_RUN(test_render_allocates_nothing);
    BT_REPORT();
}
