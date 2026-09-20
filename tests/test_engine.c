/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#include "bt_test.h"
#include "backtrack/bt_engine.h"

#define SR 48000

/* ------------------------------------------------------------- scaffolding */

typedef struct {
    float  *buf[BT_MAX_OUT_CH];
    float  *ptr[BT_MAX_OUT_CH];
    int32_t nch;
    bt_frame frames;
} outbuf;

static void out_alloc(outbuf *o, int32_t nch, bt_frame frames) {
    o->nch = nch;
    o->frames = frames;
    for (int32_t c = 0; c < nch; c++) {
        o->buf[c] = (float *)calloc((size_t)frames, sizeof(float));
        o->ptr[c] = o->buf[c];
    }
}

static void out_free(outbuf *o) {
    for (int32_t c = 0; c < o->nch; c++) free(o->buf[c]);
}

/* Renders `frames` in blocks of `block`, accumulating into o. */
static void render_all(bt_engine *e, outbuf *o, int32_t block) {
    float *win[BT_MAX_OUT_CH];
    for (bt_frame pos = 0; pos < o->frames; pos += block) {
        int32_t n = (int32_t)((o->frames - pos < block) ? (o->frames - pos) : block);
        for (int32_t c = 0; c < o->nch; c++) win[c] = o->buf[c] + pos;
        bt_engine_render(e, win, n);
    }
}

static bt_track mk_click(const char *bus) {
    bt_track t;
    memset(&t, 0, sizeof(t));
    t.type = BT_TRACK_CLICK;
    snprintf(t.name, sizeof(t.name), "Click");
    snprintf(t.bus, sizeof(t.bus), "%s", bus);
    return t;
}

/* An audio track holding a single full-scale impulse at `at`. Impulses make
 * alignment failures obvious and exact rather than approximately audible. */
static bt_track mk_impulse(const char *bus, int32_t channels, bt_frame frames,
                           bt_frame at, int32_t offset_ms) {
    bt_track t;
    memset(&t, 0, sizeof(t));
    t.type = BT_TRACK_AUDIO;
    snprintf(t.name, sizeof(t.name), "Imp");
    snprintf(t.bus, sizeof(t.bus), "%s", bus);
    snprintf(t.file, sizeof(t.file), "imp.wav");
    t.offset_ms = offset_ms;
    t.channels = channels;
    t.frames = frames;
    t.pcm = (float **)calloc((size_t)channels, sizeof(float *));
    for (int32_t c = 0; c < channels; c++) {
        t.pcm[c] = (float *)calloc((size_t)frames, sizeof(float));
        if (at >= 0 && at < frames) t.pcm[c][at] = 1.0f;
    }
    return t;
}

static bt_song mk_song(double bpm, int32_t sig, int32_t count_in) {
    bt_song s;
    memset(&s, 0, sizeof(s));
    snprintf(s.title, sizeof(s.title), "T");
    s.tempo.seg[0].bpm = bpm;
    s.tempo.nseg = 1;
    s.tempo.sig_num = sig;
    s.tempo.sig_den = 4;
    s.count_in_bars = count_in;
    return s;
}

static bt_device_cfg mk_dev(void) {
    bt_device_cfg d;
    memset(&d, 0, sizeof(d));
    d.sample_rate = SR;
    snprintf(d.bus[0].name, BT_MAX_NAME, "foh");
    d.bus[0].ch[0] = 0; d.bus[0].ch[1] = 1; d.bus[0].nch = 2;
    snprintf(d.bus[1].name, BT_MAX_NAME, "inear");
    d.bus[1].ch[0] = 2; d.bus[1].ch[1] = 3; d.bus[1].nch = 2;
    snprintf(d.bus[2].name, BT_MAX_NAME, "mono");
    d.bus[2].ch[0] = 4; d.bus[2].nch = 1;
    d.nbuses = 3;
    return d;
}

static bt_engine *mk_engine(int32_t nch, int32_t maxblock) {
    bt_engine_cfg c = { SR, nch, maxblock };
    bt_engine *e = NULL;
    BT_CHECK_EQI(bt_engine_create(&c, &e), BT_OK);
    return e;
}

/* Index of the largest absolute sample in [0, n). */
static bt_frame peak_at(const float *p, bt_frame n) {
    bt_frame best = -1;
    float bv = 0.0f;
    for (bt_frame i = 0; i < n; i++) {
        float a = fabsf(p[i]);
        if (a > bv) { bv = a; best = i; }
    }
    return best;
}

/* First frame at or after `from` whose magnitude exceeds `thresh`. */
static bt_frame onset_at(const float *p, bt_frame n, bt_frame from, float thresh) {
    for (bt_frame i = from; i < n; i++) if (fabsf(p[i]) > thresh) return i;
    return -1;
}

/* Asserts a click burst begins exactly at `beat` and that nothing sounds in
 * the 1000 frames before it.
 *
 * The burst's very first sample is legitimately zero - the synthesised click
 * is a sine starting at phase zero, so it does not add a discontinuity of its
 * own - so "begins at `beat`" means silence up to `beat` and energy within a
 * couple of samples after it, not a non-zero sample exactly on it. */
static void check_click_at(const float *p, bt_frame n, bt_frame beat) {
    bt_frame from = beat > 1000 ? beat - 1000 : 0;
    for (bt_frame i = from; i < beat; i++) {
        bt_checks++;
        if (fabsf(p[i]) > 1e-6f) {
            bt_fails++;
            fprintf(stderr, "  FAIL click sounds %lld frames early (beat %lld)\n",
                    (long long)(beat - i), (long long)beat);
            break;
        }
    }
    bt_frame got = onset_at(p, n, beat, 0.05f);
    bt_checks++;
    if (got < beat || got > beat + 2) {
        bt_fails++;
        fprintf(stderr, "  FAIL click onset %lld, expected beat at %lld\n",
                (long long)got, (long long)beat);
    }
}

/* ------------------------------------------------------------------- tests */

static void test_click_lands_on_the_beat(void) {
    bt_song s = mk_song(120.0, 4, 0);          /* 24000 frames per beat */
    s.track[0] = mk_click("inear");
    s.ntracks = 1;

    /* A click-only song has no audio, so give it an explicit length by
     * adding a silent stem that outlives the beats we want to hear. */
    s.track[1] = mk_impulse("foh", 1, SR * 3, -1, 0);
    s.ntracks = 2;

    bt_device_cfg d = mk_dev();
    bt_engine *e = mk_engine(6, 512);
    BT_CHECK_EQI(bt_engine_set_song(e, &s, &d), BT_OK);
    bt_engine_play(e);

    outbuf o;
    out_alloc(&o, 6, SR * 2);
    render_all(e, &o, 512);

    /* Click goes to the in-ear bus only - the whole point of the routing. */
    BT_CHECK(onset_at(o.buf[0], o.frames, 0, 1e-6f) == -1);
    BT_CHECK(onset_at(o.buf[1], o.frames, 0, 1e-6f) == -1);

    const bt_frame want[4] = { 0, 24000, 48000, 72000 };
    for (int i = 0; i < 4; i++) check_click_at(o.buf[2], o.frames, want[i]);
    /* Stereo in-ear bus gets both sides. */
    check_click_at(o.buf[3], o.frames, 24000);

    out_free(&o);
    bt_engine_destroy(e);
    bt_song_free_audio(&s);
}

static void test_downbeat_is_accented(void) {
    bt_song s = mk_song(120.0, 4, 0);
    s.track[0] = mk_click("inear");
    s.track[1] = mk_impulse("foh", 1, SR * 3, -1, 0);
    s.ntracks = 2;

    bt_device_cfg d = mk_dev();
    bt_engine *e = mk_engine(6, 1024);
    BT_CHECK_EQI(bt_engine_set_song(e, &s, &d), BT_OK);
    bt_engine_play(e);

    outbuf o;
    out_alloc(&o, 6, SR);
    render_all(e, &o, 1024);

    /* The accent is a different pitch, so it crosses zero at a different rate;
     * comparing zero crossings in the first 200 frames distinguishes them
     * without depending on any particular libm. */
    int zc_down = 0, zc_beat = 0;
    for (int i = 1; i < 200; i++) {
        if ((o.buf[2][i] > 0) != (o.buf[2][i - 1] > 0)) zc_down++;
        bt_frame b = 24000;
        if ((o.buf[2][b + i] > 0) != (o.buf[2][b + i - 1] > 0)) zc_beat++;
    }
    BT_CHECK(zc_down > zc_beat);

    out_free(&o);
    bt_engine_destroy(e);
    bt_song_free_audio(&s);
}

static void test_track_offset_shifts_exactly(void) {
    bt_device_cfg d = mk_dev();

    const struct { int32_t off_ms; bt_frame want; } cases[] = {
        {   0,  1000 },
        {  10,  1000 + 480 },     /* +10 ms at 48 kHz = 480 frames */
        { -10,  1000 - 480 },
    };

    for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
        bt_song s = mk_song(120.0, 4, 0);
        s.track[0] = mk_impulse("foh", 1, SR, 1000, cases[k].off_ms);
        s.ntracks = 1;

        bt_engine *e = mk_engine(6, 256);
        BT_CHECK_EQI(bt_engine_set_song(e, &s, &d), BT_OK);
        bt_engine_play(e);

        outbuf o;
        out_alloc(&o, 6, SR);
        render_all(e, &o, 256);

        BT_CHECK_EQI(peak_at(o.buf[0], o.frames), cases[k].want);
        BT_CHECK_NEAR(o.buf[0][cases[k].want], 1.0, 1e-6);

        out_free(&o);
        bt_engine_destroy(e);
        bt_song_free_audio(&s);
    }
}

static void test_gain_and_mute(void) {
    bt_device_cfg d = mk_dev();

    bt_song s = mk_song(120.0, 4, 0);
    s.track[0] = mk_impulse("foh", 1, SR, 100, 0);
    s.track[0].gain_db = -6.0206;              /* exactly half */
    s.track[1] = mk_impulse("mono", 1, SR, 200, 0);
    s.track[1].muted = true;
    s.ntracks = 2;

    bt_engine *e = mk_engine(6, 512);
    BT_CHECK_EQI(bt_engine_set_song(e, &s, &d), BT_OK);
    bt_engine_play(e);

    outbuf o;
    out_alloc(&o, 6, SR);
    render_all(e, &o, 512);

    BT_CHECK_NEAR(o.buf[0][100], 0.5, 1e-4);
    BT_CHECK_NEAR(o.buf[4][200], 0.0, 1e-9);   /* muted stays silent */

    out_free(&o);
    bt_engine_destroy(e);
    bt_song_free_audio(&s);
}

static void test_channel_folding(void) {
    bt_device_cfg d = mk_dev();

    /* Stereo stem into a mono bus sums and halves, keeping headroom. */
    bt_song s = mk_song(120.0, 4, 0);
    s.track[0] = mk_impulse("mono", 2, SR, 300, 0);
    s.ntracks = 1;

    bt_engine *e = mk_engine(6, 512);
    BT_CHECK_EQI(bt_engine_set_song(e, &s, &d), BT_OK);
    bt_engine_play(e);

    outbuf o;
    out_alloc(&o, 6, SR);
    render_all(e, &o, 512);
    BT_CHECK_NEAR(o.buf[4][300], 1.0, 1e-5);   /* 0.5 + 0.5 */
    out_free(&o);
    bt_engine_destroy(e);
    bt_song_free_audio(&s);

    /* Mono stem into a stereo bus feeds both sides at full level. */
    bt_song s2 = mk_song(120.0, 4, 0);
    s2.track[0] = mk_impulse("foh", 1, SR, 300, 0);
    s2.ntracks = 1;

    bt_engine *e2 = mk_engine(6, 512);
    BT_CHECK_EQI(bt_engine_set_song(e2, &s2, &d), BT_OK);
    bt_engine_play(e2);

    outbuf o2;
    out_alloc(&o2, 6, SR);
    render_all(e2, &o2, 512);
    BT_CHECK_NEAR(o2.buf[0][300], 1.0, 1e-6);
    BT_CHECK_NEAR(o2.buf[1][300], 1.0, 1e-6);
    out_free(&o2);
    bt_engine_destroy(e2);
    bt_song_free_audio(&s2);
}

static void test_count_in_is_click_only(void) {
    /* Two bars of 4 at 120 BPM = 8 beats = 192000 frames of count-in, during
     * which the click sounds and the band hears no backing track. */
    bt_song s = mk_song(120.0, 4, 2);
    s.track[0] = mk_click("inear");
    s.track[1] = mk_impulse("foh", 1, SR, 0, 0);   /* impulse at song frame 0 */
    s.ntracks = 2;

    bt_device_cfg d = mk_dev();
    bt_engine *e = mk_engine(6, 512);
    BT_CHECK_EQI(bt_engine_set_song(e, &s, &d), BT_OK);
    bt_engine_start_with_count_in(e);

    BT_CHECK_EQI(bt_engine_playhead(e), -192000);

    outbuf o;
    out_alloc(&o, 6, 192000 + SR);
    render_all(e, &o, 512);

    /* Click on every count-in beat... */
    for (int i = 0; i < 8; i++) check_click_at(o.buf[2], o.frames, (bt_frame)i * 24000);
    /* ...and the stem starts exactly where the count-in ends. */
    BT_CHECK_EQI(peak_at(o.buf[0], o.frames), 192000);

    out_free(&o);
    bt_engine_destroy(e);
    bt_song_free_audio(&s);
}

static void test_stops_at_end(void) {
    bt_song s = mk_song(120.0, 4, 0);
    s.track[0] = mk_impulse("foh", 1, 5000, 100, 0);
    s.ntracks = 1;

    bt_device_cfg d = mk_dev();
    bt_engine *e = mk_engine(6, 512);
    BT_CHECK_EQI(bt_engine_set_song(e, &s, &d), BT_OK);
    bt_engine_play(e);
    BT_CHECK(!bt_engine_finished(e));

    outbuf o;
    out_alloc(&o, 6, 8000);
    render_all(e, &o, 512);

    BT_CHECK(bt_engine_finished(e));
    BT_CHECK(!bt_engine_playing(e));
    /* Nothing after the end of the stem. */
    BT_CHECK(onset_at(o.buf[0], o.frames, 5000, 1e-6f) == -1);

    out_free(&o);
    bt_engine_destroy(e);
    bt_song_free_audio(&s);
}

static void test_stopped_renders_silence(void) {
    bt_song s = mk_song(120.0, 4, 0);
    s.track[0] = mk_click("inear");
    s.track[1] = mk_impulse("foh", 1, SR, 10, 0);
    s.ntracks = 2;

    bt_device_cfg d = mk_dev();
    bt_engine *e = mk_engine(6, 512);
    BT_CHECK_EQI(bt_engine_set_song(e, &s, &d), BT_OK);
    /* Never started. */
    outbuf o;
    out_alloc(&o, 6, 4096);
    render_all(e, &o, 512);
    for (int32_t c = 0; c < 6; c++)
        BT_CHECK(onset_at(o.buf[c], o.frames, 0, 1e-9f) == -1);
    BT_CHECK_EQI(bt_engine_playhead(e), 0);

    out_free(&o);
    bt_engine_destroy(e);
    bt_song_free_audio(&s);
}

static void test_block_size_invariance(void) {
    /* The same song must render bit-identically whatever buffer size the
     * interface hands us. If it does not, something is carrying state across
     * a block boundary that should not be. */
    const int32_t blocks[] = { 32, 64, 128, 256, 512, 1024, 4096 };
    const bt_frame LEN = 200000;
    bt_device_cfg d = mk_dev();

    float *ref = NULL;
    for (size_t k = 0; k < sizeof(blocks) / sizeof(blocks[0]); k++) {
        bt_song s = mk_song(137.3, 4, 1);
        s.track[0] = mk_click("inear");
        s.track[1] = mk_impulse("foh", 2, LEN, 12345, 7);
        s.ntracks = 2;

        bt_engine *e = mk_engine(6, 4096);
        BT_CHECK_EQI(bt_engine_set_song(e, &s, &d), BT_OK);
        bt_engine_start_with_count_in(e);

        outbuf o;
        out_alloc(&o, 6, LEN);
        render_all(e, &o, blocks[k]);

        if (!ref) {
            ref = (float *)malloc((size_t)LEN * sizeof(float));
            memcpy(ref, o.buf[2], (size_t)LEN * sizeof(float));
        } else {
            BT_CHECK(memcmp(ref, o.buf[2], (size_t)LEN * sizeof(float)) == 0);
        }

        out_free(&o);
        bt_engine_destroy(e);
        bt_song_free_audio(&s);
    }
    free(ref);
}

static void test_unknown_bus_is_rejected(void) {
    bt_song s = mk_song(120.0, 4, 0);
    s.track[0] = mk_click("monitor-wedge-3");    /* not in device.json */
    s.ntracks = 1;

    bt_device_cfg d = mk_dev();
    bt_engine *e = mk_engine(6, 512);
    /* Surfaced at load, not as a silent dropout at the gig. */
    BT_CHECK_EQI(bt_engine_set_song(e, &s, &d), BT_ERR_NOT_FOUND);
    bt_engine_destroy(e);
}

static void test_panic_silences(void) {
    bt_song s = mk_song(120.0, 4, 0);
    s.track[0] = mk_click("inear");
    s.track[1] = mk_impulse("foh", 1, SR, 10, 0);
    s.ntracks = 2;

    bt_device_cfg d = mk_dev();
    bt_engine *e = mk_engine(6, 512);
    BT_CHECK_EQI(bt_engine_set_song(e, &s, &d), BT_OK);
    bt_engine_play(e);

    outbuf o;
    out_alloc(&o, 6, 2048);
    render_all(e, &o, 512);
    BT_CHECK(bt_engine_playing(e));

    bt_engine_panic(e);
    BT_CHECK(!bt_engine_playing(e));
    BT_CHECK_EQI(bt_engine_playhead(e), 0);

    outbuf o2;
    out_alloc(&o2, 6, 2048);
    render_all(e, &o2, 512);
    for (int32_t c = 0; c < 6; c++)
        BT_CHECK(onset_at(o2.buf[c], o2.frames, 0, 1e-9f) == -1);

    out_free(&o); out_free(&o2);
    bt_engine_destroy(e);
    bt_song_free_audio(&s);
}

int main(void) {
    BT_RUN(test_click_lands_on_the_beat);
    BT_RUN(test_downbeat_is_accented);
    BT_RUN(test_track_offset_shifts_exactly);
    BT_RUN(test_gain_and_mute);
    BT_RUN(test_channel_folding);
    BT_RUN(test_count_in_is_click_only);
    BT_RUN(test_stops_at_end);
    BT_RUN(test_stopped_renders_silence);
    BT_RUN(test_block_size_invariance);
    BT_RUN(test_unknown_bus_is_rejected);
    BT_RUN(test_panic_silences);
    BT_REPORT();
}
