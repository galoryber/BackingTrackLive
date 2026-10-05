/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * The set list player: song selection, end-of-song behaviour and the preload
 * window. All of it offline - the device never enters into it.
 */
#include "bt_test.h"
#include "backtrack/bt_player.h"
#include "backtrack/bt_thread.h"
#include "backtrack/bt_wav.h"

#define SR       48000
#define SONGS    6
#define SONG_SEC 0.2

static char g_setlist_path[] = "player_setlist.json";

/* Fixtures live flat in the working directory rather than a subdirectory, so
 * the test needs no portable mkdir to run identically on Windows CI. */
static void stem_name(char *dst, size_t cap, int32_t song) {
    snprintf(dst, cap, "player_stem_%d.wav", song);
}

static void write_stem(int32_t song) {
    const bt_frame n = (bt_frame)(SR * SONG_SEC);
    float *buf = (float *)malloc((size_t)n * sizeof(float));
    bt_lcg_seed((uint32_t)song + 1u);
    for (bt_frame i = 0; i < n; i++) buf[i] = bt_lcg_sample();

    char path[64];
    stem_name(path, sizeof(path), song);
    const float *p[1] = { buf };
    BT_CHECK_EQI(bt_wav_write_file(path, p, 1, SR, n), BT_OK);
    free(buf);
}

/* Builds a set list of SONGS songs; `next_mask` bit i sets song i to
 * on_end: next. */
static void build_fixture(uint32_t next_mask) {
    char json[8192];
    size_t o = 0;
    o += (size_t)snprintf(json + o, sizeof(json) - o,
                          "{\"version\":1,\"name\":\"P\",\"songs\":[");
    for (int32_t i = 0; i < SONGS; i++) {
        char stem[64];
        stem_name(stem, sizeof(stem), i);
        write_stem(i);
        o += (size_t)snprintf(json + o, sizeof(json) - o,
            "%s{\"title\":\"S%d\",\"tempo\":{\"bpm\":120},"
            "\"count_in_bars\":1,\"on_end\":\"%s\",\"tracks\":["
            "{\"type\":\"click\",\"bus\":\"inear\"},"
            "{\"type\":\"audio\",\"bus\":\"foh\",\"file\":\"%s\"}]}",
            i ? "," : "", i,
            (next_mask & (1u << i)) ? "next" : "stop", stem);
    }
    o += (size_t)snprintf(json + o, sizeof(json) - o, "]}");

    FILE *f = fopen(g_setlist_path, "wb");
    BT_CHECK(f != NULL);
    if (f) { fwrite(json, 1, o, f); fclose(f); }
}

static void cleanup_fixture(void) {
    remove(g_setlist_path);
    for (int32_t i = 0; i < SONGS; i++) {
        char path[64];
        stem_name(path, sizeof(path), i);
        remove(path);
    }
}

static bt_device_cfg mk_dev(void) {
    bt_device_cfg d;
    memset(&d, 0, sizeof(d));
    d.sample_rate = SR;
    snprintf(d.bus[0].name, BT_MAX_NAME, "foh");
    d.bus[0].ch[0] = 0; d.bus[0].ch[1] = 1; d.bus[0].nch = 2;
    snprintf(d.bus[1].name, BT_MAX_NAME, "inear");
    d.bus[1].ch[0] = 2; d.bus[1].ch[1] = 3; d.bus[1].nch = 2;
    d.nbuses = 2;
    return d;
}

typedef struct {
    bt_setlist *sl;
    bt_player  *p;
    float      *buf[4];
    float      *win[4];
} rig;

static void rig_up(rig *r, uint32_t next_mask, int32_t preload_ahead) {
    build_fixture(next_mask);

    int line = 0;
    r->sl = NULL;
    BT_CHECK_EQI(bt_setlist_load_file(g_setlist_path, &r->sl, &line), BT_OK);

    bt_device_cfg d = mk_dev();
    bt_player_cfg c = { SR, 4, 1024, preload_ahead, 10000 };
    r->p = NULL;
    BT_CHECK_EQI(bt_player_create(&c, r->sl, &d, &r->p), BT_OK);

    for (int i = 0; i < 4; i++) {
        r->buf[i] = (float *)calloc(1024, sizeof(float));
        r->win[i] = r->buf[i];
    }
}

static void rig_down(rig *r) {
    for (int i = 0; i < 4; i++) free(r->buf[i]);
    bt_player_destroy(r->p);
    bt_setlist_free(r->sl);
    cleanup_fixture();
}

/* Loading happens on the loader thread now, so residency is something you
 * wait for rather than something a tick performs. These bound the wait so a
 * genuine failure is a test failure and not a hang. */
static bool wait_until(rig *r, int32_t song, bool want, int ms) {
    for (int i = 0; i < ms; i++) {
        bt_tick_result t = BT_TICK_IDLE;
        bt_player_tick(r->p, &t);
        if (bt_player_song_resident(r->p, song) == want) return true;
        bt_thread_sleep_ms(1);
    }
    return false;
}

static void check_resident(rig *r, int32_t song, bool want, const char *what) {
    bt_checks++;
    if (!wait_until(r, song, want, 5000)) {
        bt_fails++;
        fprintf(stderr, "  FAIL song %d should%s be resident (%s)\n",
                song, want ? "" : " not", what);
    }
}

/* Renders up to `blocks` blocks, ticking between each, stopping once `tick`
 * reports something other than idle/preload. Returns that result. */
static bt_tick_result run_until_event(rig *r, int blocks) {
    for (int i = 0; i < blocks; i++) {
        bt_player_render(r->p, r->win, 1024);
        bt_tick_result t = BT_TICK_IDLE;
        BT_CHECK_EQI(bt_player_tick(r->p, &t), BT_OK);
        if (t == BT_TICK_SONG_ENDED || t == BT_TICK_ADVANCED) return t;
    }
    return BT_TICK_IDLE;
}

/* ------------------------------------------------------------------ tests */

static void test_select_and_navigate(void) {
    rig r;
    rig_up(&r, 0, 1);

    BT_CHECK_EQI(bt_player_current(r.p), -1);
    BT_CHECK_EQI(bt_player_count(r.p), SONGS);

    BT_CHECK_EQI(bt_player_select(r.p, 2), BT_OK);
    BT_CHECK_EQI(bt_player_current(r.p), 2);
    BT_CHECK(bt_player_song(r.p) != NULL);
    BT_CHECK(strcmp(bt_player_song(r.p)->title, "S2") == 0);
    BT_CHECK(!bt_player_playing(r.p));

    BT_CHECK_EQI(bt_player_next(r.p), BT_OK);
    BT_CHECK_EQI(bt_player_current(r.p), 3);
    BT_CHECK_EQI(bt_player_prev(r.p), BT_OK);
    BT_CHECK_EQI(bt_player_current(r.p), 2);

    /* Out of range is refused, not clamped silently. */
    BT_CHECK(bt_player_select(r.p, -1) != BT_OK);
    BT_CHECK(bt_player_select(r.p, SONGS) != BT_OK);
    BT_CHECK_EQI(bt_player_current(r.p), 2);

    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);
    BT_CHECK(bt_player_prev(r.p) != BT_OK);
    BT_CHECK_EQI(bt_player_select(r.p, SONGS - 1), BT_OK);
    BT_CHECK(bt_player_next(r.p) != BT_OK);

    rig_down(&r);
}

static void test_preload_window(void) {
    rig r;
    rig_up(&r, 0, 1);

    /* select() waits for the song it selects - that one is needed now. */
    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);
    BT_CHECK(bt_player_song_resident(r.p, 0));

    /* The rest of the window arrives behind it, on the loader thread. */
    check_resident(&r, 1, true,  "next song preloads");
    check_resident(&r, 2, false, "outside the window");
    check_resident(&r, 5, false, "outside the window");

    /* Once settled, a tick reports no song event. It may still report that
     * the loader finished something, which is a property of when the two
     * threads happen to meet rather than of the behaviour under test. */
    bt_tick_result t = BT_TICK_IDLE;
    BT_CHECK_EQI(bt_player_tick(r.p, &t), BT_OK);
    BT_CHECK_EQI(bt_player_tick(r.p, &t), BT_OK);
    BT_CHECK(t == BT_TICK_IDLE || t == BT_TICK_PRELOADED);

    size_t two_songs = bt_player_resident_bytes(r.p);
    BT_CHECK(two_songs > 0);

    /* Jumping across the set frees what is behind and loads what is ahead. */
    BT_CHECK_EQI(bt_player_select(r.p, 4), BT_OK);
    check_resident(&r, 0, false, "freed behind");
    check_resident(&r, 1, false, "freed behind");
    check_resident(&r, 4, true,  "selected");
    check_resident(&r, 5, true,  "preloaded ahead");
    /* Still two songs' worth - the window does not grow as you move. */
    BT_CHECK_EQI(bt_player_resident_bytes(r.p), (long long)two_songs);

    rig_down(&r);
}

static void test_preload_ahead_zero(void) {
    rig r;
    rig_up(&r, 0, 0);

    BT_CHECK_EQI(bt_player_select(r.p, 2), BT_OK);
    check_resident(&r, 2, true,  "selected");
    check_resident(&r, 3, false, "no lookahead requested");

    rig_down(&r);
}

static void test_on_end_stop_waits(void) {
    rig r;
    rig_up(&r, 0, 1);          /* every song stops */

    BT_CHECK_EQI(bt_player_select(r.p, 1), BT_OK);
    bt_player_play(r.p);

    bt_tick_result t = run_until_event(&r, 200);
    BT_CHECK_EQI(t, BT_TICK_SONG_ENDED);
    BT_CHECK_EQI(bt_player_current(r.p), 1);     /* did not move */
    BT_CHECK(!bt_player_playing(r.p));

    /* The end is reported once, not on every subsequent tick.
     *
     * Not asserted as IDLE: the loader runs on its own thread, and if it
     * happens to finish something between these two ticks the honest answer
     * is PRELOADED. Demanding IDLE made this depend on which machine it ran
     * on, and a macOS runner duly disagreed. What matters is that the end
     * does not come round again. */
    BT_CHECK_EQI(bt_player_tick(r.p, &t), BT_OK);
    BT_CHECK(t != BT_TICK_SONG_ENDED);

    rig_down(&r);
}

static void test_on_end_next_advances(void) {
    rig r;
    rig_up(&r, 0x2u, 1);       /* song 1 runs into song 2 */

    BT_CHECK_EQI(bt_player_select(r.p, 1), BT_OK);
    bt_player_play(r.p);

    bt_tick_result t = run_until_event(&r, 200);
    BT_CHECK_EQI(t, BT_TICK_ADVANCED);
    BT_CHECK_EQI(bt_player_current(r.p), 2);
    BT_CHECK(bt_player_playing(r.p));
    /* The fixture's songs ask for a bar of count-in, and a segue honours what
     * the incoming song asks for - so the playhead is in the count-in, which
     * is negative. It used to jump to bar 1 regardless; that is right for two
     * songs in one tempo and wrong the moment the tempo changes, and it was
     * never this layer's call to make. */
    BT_CHECK(bt_player_playhead(r.p) < 0);

    /* Song 2 is on_end: stop, so the chain ends there rather than running on. */
    t = run_until_event(&r, 200);
    BT_CHECK_EQI(t, BT_TICK_SONG_ENDED);
    BT_CHECK_EQI(bt_player_current(r.p), 2);

    rig_down(&r);
}

static void test_on_end_next_chains(void) {
    rig r;
    rig_up(&r, 0x7u, 1);       /* songs 0,1,2 all run on */

    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);
    bt_player_play(r.p);

    BT_CHECK_EQI(run_until_event(&r, 200), BT_TICK_ADVANCED);
    BT_CHECK_EQI(bt_player_current(r.p), 1);
    BT_CHECK_EQI(run_until_event(&r, 200), BT_TICK_ADVANCED);
    BT_CHECK_EQI(bt_player_current(r.p), 2);
    BT_CHECK_EQI(run_until_event(&r, 200), BT_TICK_ADVANCED);
    BT_CHECK_EQI(bt_player_current(r.p), 3);
    /* Song 3 stops. */
    BT_CHECK_EQI(run_until_event(&r, 200), BT_TICK_SONG_ENDED);

    rig_down(&r);
}

static void test_on_end_next_at_last_song_stops(void) {
    rig r;
    rig_up(&r, 0xFFFFFFFFu, 1);   /* even the last song says "next" */

    BT_CHECK_EQI(bt_player_select(r.p, SONGS - 1), BT_OK);
    bt_player_play(r.p);

    /* There is nothing to advance to, so it behaves as stop rather than
     * wrapping to the top of the set in front of an audience. */
    BT_CHECK_EQI(run_until_event(&r, 200), BT_TICK_SONG_ENDED);
    BT_CHECK_EQI(bt_player_current(r.p), SONGS - 1);
    BT_CHECK(!bt_player_playing(r.p));

    rig_down(&r);
}

static void test_count_in_then_stop_is_not_an_end(void) {
    rig r;
    rig_up(&r, 0, 1);

    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);
    bt_player_start(r.p);                     /* count-in: negative playhead */
    BT_CHECK(bt_player_playhead(r.p) < 0);

    bt_tick_result t = BT_TICK_IDLE;
    BT_CHECK_EQI(bt_player_tick(r.p, &t), BT_OK);
    BT_CHECK(t != BT_TICK_SONG_ENDED);
    BT_CHECK(bt_player_playing(r.p));

    rig_down(&r);
}

static void test_render_and_tick_before_select(void) {
    rig r;
    rig_up(&r, 0, 1);

    /* Nothing selected: silence, no crash, nothing resident. */
    bt_player_render(r.p, r.win, 1024);
    for (int c = 0; c < 4; c++)
        for (int i = 0; i < 1024; i++) BT_CHECK(r.buf[c][i] == 0.0f);

    bt_tick_result t = BT_TICK_ADVANCED;
    BT_CHECK_EQI(bt_player_tick(r.p, &t), BT_OK);
    BT_CHECK_EQI(t, BT_TICK_IDLE);
    BT_CHECK_EQI(bt_player_resident_bytes(r.p), 0);
    BT_CHECK(!bt_player_playing(r.p));

    rig_down(&r);
}

static void test_panic_from_player(void) {
    rig r;
    rig_up(&r, 0, 1);

    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);
    bt_player_play(r.p);
    bt_player_render(r.p, r.win, 1024);
    BT_CHECK(bt_player_playing(r.p));

    bt_player_panic(r.p);
    BT_CHECK(!bt_player_playing(r.p));
    BT_CHECK_EQI(bt_player_playhead(r.p), 0);

    bt_player_render(r.p, r.win, 1024);
    for (int c = 0; c < 4; c++)
        for (int i = 0; i < 1024; i++) BT_CHECK(r.buf[c][i] == 0.0f);

    rig_down(&r);
}


/* Nudging a stem while the song plays must change where that stem sounds
 * without moving the playhead or stopping the music. That is the whole
 * alignment workflow: hear it, nudge, hear it again from the same place -
 * which is why this is not bt_player_select, which rewinds and stops. */
static void test_reapply_keeps_position_and_playback(void) {
    rig r;
    rig_up(&r, 0, 1);
    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);

    bt_player_play(r.p);
    for (int i = 0; i < 4; i++) bt_player_render(r.p, r.win, 1024);

    const bt_frame before = bt_player_playhead(r.p);
    BT_CHECK(before > 0);
    BT_CHECK(bt_player_playing(r.p));

    /* The edit someone makes in the align view. */
    r.sl->song[0].track[1].offset_ms = -30;
    BT_CHECK_EQI(bt_player_reapply(r.p), BT_OK);

    BT_CHECK_EQI(bt_player_playhead(r.p), before);   /* did not rewind */
    BT_CHECK(bt_player_playing(r.p));                /* did not stop   */

    for (int i = 0; i < 2; i++) bt_player_render(r.p, r.win, 1024);
    BT_CHECK(bt_player_playhead(r.p) > before);      /* and carried on */

    rig_down(&r);
}

/* Reapplying while stopped must not start the music: someone editing a song
 * they are not listening to should not have it begin playing at them. */
static void test_reapply_while_stopped_stays_stopped(void) {
    rig r;
    rig_up(&r, 0, 1);
    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);

    BT_CHECK(!bt_player_playing(r.p));
    r.sl->song[0].track[1].gain_db = -6.0;
    BT_CHECK_EQI(bt_player_reapply(r.p), BT_OK);
    BT_CHECK(!bt_player_playing(r.p));
    BT_CHECK_EQI(bt_player_playhead(r.p), 0);

    rig_down(&r);
}

/* A nudge actually moves the audio. Without this the two tests above would
 * pass over a reapply that did nothing at all. */
static void test_reapply_moves_the_audio(void) {
    rig r;
    rig_up(&r, 0, 1);
    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);

    bt_player_play(r.p);
    bt_player_render(r.p, r.win, 1024);
    float first[1024];
    memcpy(first, r.buf[0], sizeof(first));

    /* Back to the top the ordinary way, so the only difference between the
     * two renders is the nudge that reapply carries. */
    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);
    r.sl->song[0].track[1].offset_ms = 50;      /* half a buffer and more */
    BT_CHECK_EQI(bt_player_reapply(r.p), BT_OK);
    bt_player_play(r.p);
    bt_player_render(r.p, r.win, 1024);

    int differs = 0;
    for (int i = 0; i < 1024; i++)
        if (fabs(first[i] - r.buf[0][i]) > 1e-6) differs++;
    BT_CHECK(differs > 0);

    rig_down(&r);
}


/* Seeking is what makes the end of a song reachable without sitting through
 * it, which is the only practical way to tell an alignment problem from a
 * tempo one. */
static void test_seek_moves_the_playhead(void) {
    rig r;
    rig_up(&r, 0, 1);
    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);

    const bt_frame len = bt_player_song_frames(r.p);
    BT_CHECK(len > 0);

    bt_player_seek(r.p, len / 2);
    BT_CHECK_EQI(bt_player_playhead(r.p), len / 2);

    /* And playback carries on from there rather than from the top. */
    bt_player_play(r.p);
    bt_player_render(r.p, r.win, 1024);
    BT_CHECK(bt_player_playhead(r.p) > len / 2);

    /* Seeking while stopped must not start it. */
    bt_player_stop(r.p);
    bt_player_seek(r.p, 0);
    BT_CHECK_EQI(bt_player_playhead(r.p), 0);
    BT_CHECK(!bt_player_playing(r.p));

    rig_down(&r);
}

/* The length a scrubber is drawn against has to be the song's, not the
 * engine's idea of where it happens to be. */
static void test_song_frames_matches_the_stem(void) {
    rig r;
    rig_up(&r, 0, 1);
    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);
    BT_CHECK_EQI(bt_player_song_frames(r.p), (bt_frame)(SR * SONG_SEC));

    /* A declared length longer than the stem extends it. */
    r.sl->song[0].length_bars = 8;      /* 32 beats at 120bpm = 16 s */
    BT_CHECK_EQI(bt_player_reapply(r.p), BT_OK);
    BT_CHECK(bt_player_song_frames(r.p) > (bt_frame)(SR * SONG_SEC));

    rig_down(&r);
}


/* wait_loaded is what a UI calls before drawing a waveform it does not yet
 * have. Resident already, it returns at once; it must not be the thing that
 * makes a screen hang. */
static void test_wait_loaded(void) {
    rig r;
    rig_up(&r, 0, 1);
    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);

    BT_CHECK_EQI(bt_player_wait_loaded(r.p, 0, 5000), BT_OK);
    BT_CHECK(bt_player_song_resident(r.p, 0));
    BT_CHECK_EQI(bt_player_wait_loaded(NULL, 0, 10), BT_ERR_RANGE);

    rig_down(&r);
}


/* A segue into a song with a count-in must play that count-in, at the new
 * song's tempo. Two songs in one tempo do not need it and a tempo change very
 * much does: those bars are what carries a band across the join. */
static void test_segue_honours_the_count_in(void) {
    rig r;
    rig_up(&r, 1u, 1);                     /* song 0 segues into song 1 */
    r.sl->song[1].count_in_bars = 2;
    r.sl->song[1].tempo.seg[0].bpm = 147.0;
    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);

    bt_player_play(r.p);
    bt_tick_result t = BT_TICK_IDLE;
    for (int i = 0; i < 400 && t != BT_TICK_ADVANCED; i++) {
        bt_player_render(r.p, r.win, 1024);
        BT_CHECK_EQI(bt_player_tick(r.p, &t), BT_OK);
    }
    BT_CHECK_EQI(t, BT_TICK_ADVANCED);
    BT_CHECK_EQI(bt_player_current(r.p), 1);

    /* Negative playhead is the count-in. Straight to bar 1 would be >= 0. */
    BT_CHECK(bt_player_playhead(r.p) < 0);
    BT_CHECK(bt_player_playing(r.p));

    /* Two bars of 4/4 at 147bpm is 8 beats, a shade over 3.2 seconds. */
    const double lead = -(double)bt_player_playhead(r.p) / SR;
    BT_CHECK_NEAR(lead, 8.0 * 60.0 / 147.0, 0.02);

    rig_down(&r);
}

/* And a song that asks for no count-in still runs straight in, which is what
 * two musically continuous songs want. */
static void test_segue_without_count_in_goes_straight_in(void) {
    rig r;
    rig_up(&r, 1u, 1);
    r.sl->song[1].count_in_bars = 0;
    /* A downbeat offset is what separates "start the song" from "start at
     * beat 1": with no count-in the song must begin at frame 0, or the audio
     * before its first downbeat is silently skipped. */
    r.sl->song[1].tempo.downbeat_ms = 250.0;
    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);

    bt_player_play(r.p);
    bt_tick_result t = BT_TICK_IDLE;
    for (int i = 0; i < 400 && t != BT_TICK_ADVANCED; i++) {
        bt_player_render(r.p, r.win, 1024);
        BT_CHECK_EQI(bt_player_tick(r.p, &t), BT_OK);
    }
    BT_CHECK_EQI(t, BT_TICK_ADVANCED);
    BT_CHECK_EQI(bt_player_current(r.p), 1);
    BT_CHECK(bt_player_playing(r.p));
    /* At the top of the song, not 250 ms into it. */
    BT_CHECK(bt_player_playhead(r.p) >= 0);
    BT_CHECK(bt_player_playhead(r.p) < (bt_frame)(SR * 0.05));

    rig_down(&r);
}


/* Jumping backwards through a set: the preload window holds the current song
 * and the next, so an earlier one has been freed by the time you come back to
 * it. Something has to ask for it again, without blocking the caller. */
static void test_request_loads_a_song_that_was_freed(void) {
    rig r;
    rig_up(&r, 0, 1);

    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);
    BT_CHECK(bt_player_song_resident(r.p, 0));

    /* Walk far enough away that song 0 falls out of the window. */
    BT_CHECK_EQI(bt_player_select(r.p, 4), BT_OK);
    BT_CHECK_EQI(bt_player_wait_loaded(r.p, 4, 5000), BT_OK);
    BT_CHECK(!bt_player_song_resident(r.p, 0));

    /* Asking for it must not move the engine off the song that is bound. */
    BT_CHECK_EQI(bt_player_request(r.p, 0), BT_OK);
    BT_CHECK_EQI(bt_player_current(r.p), 4);

    /* And it must actually arrive - polled, not waited on. bt_loader_wait
     * widens the window itself when the song is not wanted, so waiting would
     * request the load and pass whether or not request did anything, which is
     * exactly the bug this exists to catch. */
    bool arrived = false;
    for (int i = 0; i < 250 && !arrived; i++) {
        arrived = bt_player_song_resident(r.p, 0);
        if (!arrived) bt_thread_sleep_ms(20);
    }
    BT_CHECK(arrived);

    /* Selecting it afterwards is then immediate. */
    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);
    BT_CHECK_EQI(bt_player_current(r.p), 0);

    rig_down(&r);
}

static void test_request_rejects_nonsense(void) {
    rig r;
    rig_up(&r, 0, 1);
    BT_CHECK_EQI(bt_player_request(NULL, 0), BT_ERR_RANGE);
    BT_CHECK_EQI(bt_player_request(r.p, -1), BT_ERR_RANGE);
    BT_CHECK_EQI(bt_player_request(r.p, SONGS), BT_ERR_RANGE);
    rig_down(&r);
}


/* Adding a stem to a song that had none. A song with nothing to load counts
 * as resident from the moment the loader starts - which a brand new song,
 * holding only a click, is - so without being told that its tracks changed
 * the loader has no reason to load anything, the stem's audio never arrives,
 * and selecting the song fails because a track has no PCM.
 *
 * That shipped: the align view reported the song was not loaded, and loading
 * it failed with an error that said "ok".
 */
static void test_reload_picks_up_a_newly_added_stem(void) {
    rig r;
    rig_up(&r, 0, 1);

    /* Strip song 0 back to a click, as a new song is. */
    r.sl->song[0].ntracks = 1;
    r.sl->song[0].track[0].type = BT_TRACK_CLICK;
    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);
    BT_CHECK(bt_player_song_resident(r.p, 0));

    /* Now add a stem, the way the editor does. */
    bt_track *t = &r.sl->song[0].track[1];
    memset(t, 0, sizeof(*t));
    t->type = BT_TRACK_AUDIO;
    snprintf(t->name, BT_MAX_NAME, "Synth");
    snprintf(t->bus, BT_MAX_NAME, "foh");
    stem_name(t->file, sizeof(t->file), 0);
    r.sl->song[0].ntracks = 2;

    /* Without being told, the loader still believes it is done - so the song
     * cannot be selected, because a track has no audio. Asserted through
     * select rather than by reading track.pcm: only the loader thread may
     * touch that, and reading it from here is the race ThreadSanitizer
     * correctly complains about. */
    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_ERR_STATE);

    BT_CHECK_EQI(bt_player_reload(r.p, 0), BT_OK);

    bool ready = false;
    for (int i = 0; i < 250 && !ready; i++) {
        ready = bt_player_song_resident(r.p, 0);
        if (!ready) bt_thread_sleep_ms(20);
    }
    BT_CHECK(ready);

    /* And now it selects, which is only possible once every audio track has
     * its PCM - which is the thing this test is really about. */
    BT_CHECK_EQI(bt_player_select(r.p, 0), BT_OK);

    rig_down(&r);
}

static void test_reload_rejects_nonsense(void) {
    rig r;
    rig_up(&r, 0, 1);
    BT_CHECK_EQI(bt_player_reload(NULL, 0), BT_ERR_RANGE);
    BT_CHECK_EQI(bt_player_reload(r.p, -1), BT_ERR_RANGE);
    BT_CHECK_EQI(bt_player_reload(r.p, SONGS), BT_ERR_RANGE);
    rig_down(&r);
}

int main(void) {
    BT_RUN(test_reload_picks_up_a_newly_added_stem);
    BT_RUN(test_reload_rejects_nonsense);
    BT_RUN(test_request_loads_a_song_that_was_freed);
    BT_RUN(test_request_rejects_nonsense);
    BT_RUN(test_segue_honours_the_count_in);
    BT_RUN(test_segue_without_count_in_goes_straight_in);
    BT_RUN(test_wait_loaded);
    BT_RUN(test_seek_moves_the_playhead);
    BT_RUN(test_song_frames_matches_the_stem);
    BT_RUN(test_reapply_keeps_position_and_playback);
    BT_RUN(test_reapply_while_stopped_stays_stopped);
    BT_RUN(test_reapply_moves_the_audio);
    BT_RUN(test_select_and_navigate);
    BT_RUN(test_preload_window);
    BT_RUN(test_preload_ahead_zero);
    BT_RUN(test_on_end_stop_waits);
    BT_RUN(test_on_end_next_advances);
    BT_RUN(test_on_end_next_chains);
    BT_RUN(test_on_end_next_at_last_song_stops);
    BT_RUN(test_count_in_then_stop_is_not_an_end);
    BT_RUN(test_render_and_tick_before_select);
    BT_RUN(test_panic_from_player);
    BT_REPORT();
}
