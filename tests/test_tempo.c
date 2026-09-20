/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Tempo is the one piece of arithmetic in this program that, if it is subtly
 * wrong, is wrong on stage and nowhere else. These tests are deliberately
 * fussy about exact sample positions. */
#include "bt_test.h"
#include "backtrack/bt_model.h"

static bt_tempo_map simple(double bpm, int32_t num, double downbeat_ms) {
    bt_tempo_map tm;
    memset(&tm, 0, sizeof(tm));
    tm.seg[0].bpm = bpm;
    tm.seg[0].start_beat = 0;
    tm.nseg = 1;
    tm.sig_num = num;
    tm.sig_den = 4;
    tm.downbeat_ms = downbeat_ms;
    return tm;
}

static void test_exact_positions(void) {
    /* 120 BPM at 48 kHz is exactly 24000 frames per beat. */
    bt_tempo_map tm = simple(120.0, 4, 0.0);
    BT_CHECK_EQI(bt_tempo_beat_frame(&tm, 0, 48000), 0);
    BT_CHECK_EQI(bt_tempo_beat_frame(&tm, 1, 48000), 24000);
    BT_CHECK_EQI(bt_tempo_beat_frame(&tm, 4, 48000), 96000);
    BT_CHECK_EQI(bt_tempo_beat_frame(&tm, 100, 48000), 2400000);
}

static void test_downbeat_offset(void) {
    /* Downloaded stems routinely start with silence; the offset is what makes
     * the click land on the music rather than on the file. */
    bt_tempo_map tm = simple(120.0, 4, 500.0);   /* beat 0 at 0.5 s */
    BT_CHECK_EQI(bt_tempo_beat_frame(&tm, 0, 48000), 24000);
    BT_CHECK_EQI(bt_tempo_beat_frame(&tm, 1, 48000), 48000);
}

static void test_count_in_is_negative(void) {
    bt_tempo_map tm = simple(120.0, 4, 0.0);
    BT_CHECK_EQI(bt_tempo_beat_frame(&tm, -1, 48000), -24000);
    BT_CHECK_EQI(bt_tempo_beat_frame(&tm, -4, 48000), -96000);
    BT_CHECK_EQI(bt_tempo_beat_frame(&tm, -8, 48000), -192000);
}

static void test_no_drift_over_a_set(void) {
    /* A deliberately ugly tempo, three hours in. If beat positions were
     * accumulated rather than computed, this is where it would show. */
    const double bpm = 156.37;
    const int32_t sr = 48000;
    bt_tempo_map tm = simple(bpm, 4, 0.0);

    for (int64_t beat = 0; beat < 30000; beat += 997) {
        double want = (double)beat * 60.0 / bpm * (double)sr;
        bt_frame got = bt_tempo_beat_frame(&tm, beat, sr);
        /* One sample of rounding, and no more - not one sample per beat. */
        BT_CHECK_NEAR((double)got, want, 1.0);
    }
}

static void test_downbeat_detection(void) {
    bt_tempo_map four = simple(120.0, 4, 0.0);
    BT_CHECK(bt_tempo_is_downbeat(&four, 0));
    BT_CHECK(!bt_tempo_is_downbeat(&four, 1));
    BT_CHECK(bt_tempo_is_downbeat(&four, 8));
    /* Count-in beats are negative; floored modulo keeps the accent on the 1. */
    BT_CHECK(bt_tempo_is_downbeat(&four, -4));
    BT_CHECK(bt_tempo_is_downbeat(&four, -8));
    BT_CHECK(!bt_tempo_is_downbeat(&four, -3));

    bt_tempo_map three = simple(120.0, 3, 0.0);
    BT_CHECK(bt_tempo_is_downbeat(&three, 3));
    BT_CHECK(!bt_tempo_is_downbeat(&three, 4));
    BT_CHECK(bt_tempo_is_downbeat(&three, -3));
}

static void test_tempo_map(void) {
    /* 120 for 16 beats, then 90. The song with a tempo change in the bridge
     * must work without a file-format migration. */
    bt_tempo_map tm;
    memset(&tm, 0, sizeof(tm));
    tm.seg[0].bpm = 120.0; tm.seg[0].start_beat = 0;
    tm.seg[1].bpm =  90.0; tm.seg[1].start_beat = 16;
    tm.nseg = 2;
    tm.sig_num = 4; tm.sig_den = 4;

    BT_CHECK_EQI(bt_tempo_beat_frame(&tm, 16, 48000), 16 * 24000);
    /* Beat 17 is one 90-BPM beat later: 32000 frames. */
    BT_CHECK_EQI(bt_tempo_beat_frame(&tm, 17, 48000), 16 * 24000 + 32000);
    BT_CHECK_EQI(bt_tempo_beat_frame(&tm, 20, 48000), 16 * 24000 + 4 * 32000);
}

static void test_inverse(void) {
    bt_tempo_map tm = simple(137.0, 4, 250.0);
    const int32_t sr = 44100;

    for (int64_t beat = -8; beat < 500; beat++) {
        bt_frame f = bt_tempo_beat_frame(&tm, beat, sr);
        /* On the beat, and just before the next one, both resolve back. */
        BT_CHECK_EQI(bt_tempo_frame_beat(&tm, f, sr), beat);
        bt_frame nxt = bt_tempo_beat_frame(&tm, beat + 1, sr);
        BT_CHECK_EQI(bt_tempo_frame_beat(&tm, nxt - 1, sr), beat);
    }
}

static void test_inverse_across_tempo_change(void) {
    bt_tempo_map tm;
    memset(&tm, 0, sizeof(tm));
    tm.seg[0].bpm = 100.0; tm.seg[0].start_beat = 0;
    tm.seg[1].bpm = 140.0; tm.seg[1].start_beat = 32;
    tm.seg[2].bpm =  80.0; tm.seg[2].start_beat = 64;
    tm.nseg = 3;
    tm.sig_num = 4; tm.sig_den = 4;

    for (int64_t beat = 0; beat < 120; beat++) {
        bt_frame f = bt_tempo_beat_frame(&tm, beat, 48000);
        BT_CHECK_EQI(bt_tempo_frame_beat(&tm, f, 48000), beat);
    }
}

int main(void) {
    BT_RUN(test_exact_positions);
    BT_RUN(test_downbeat_offset);
    BT_RUN(test_count_in_is_negative);
    BT_RUN(test_no_drift_over_a_set);
    BT_RUN(test_downbeat_detection);
    BT_RUN(test_tempo_map);
    BT_RUN(test_inverse);
    BT_RUN(test_inverse_across_tempo_change);
    BT_REPORT();
}
