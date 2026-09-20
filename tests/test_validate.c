/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * The set list checker. Each fixture below is a mistake somebody will
 * actually make while assembling forty songs the week before a gig.
 */
#include "bt_test.h"
#include "backtrack/bt_validate.h"
#include "backtrack/bt_wav.h"

#define SR 48000

static void write_tone(const char *path, bt_frame n, float amp,
                       int32_t rate, int32_t ch) {
    float *buf[2];
    for (int32_t c = 0; c < ch; c++) {
        buf[c] = (float *)malloc((size_t)n * sizeof(float));
        for (bt_frame i = 0; i < n; i++) {
            /* A triangle, not a sine: no libm, and the peak is exact. */
            double ph = (double)((i * 441) % rate) / (double)rate;
            double v  = (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);
            buf[c][i] = (float)(v * (double)amp);
        }
    }
    const float *p[2] = { buf[0], ch > 1 ? buf[1] : NULL };
    BT_CHECK_EQI(bt_wav_write_file(path, p, ch, rate, n), BT_OK);
    for (int32_t c = 0; c < ch; c++) free(buf[c]);
}

static void write_text(const char *path, const char *s) {
    FILE *f = fopen(path, "wb");
    BT_CHECK(f != NULL);
    if (f) { fputs(s, f); fclose(f); }
}

static bt_device_cfg mk_dev(void) {
    bt_device_cfg d;
    memset(&d, 0, sizeof(d));
    d.sample_rate = SR;
    d.buffer_frames = 512;
    snprintf(d.bus[0].name, BT_MAX_NAME, "foh");
    d.bus[0].ch[0] = 0; d.bus[0].ch[1] = 1; d.bus[0].nch = 2;
    snprintf(d.bus[1].name, BT_MAX_NAME, "inear");
    d.bus[1].ch[0] = 2; d.bus[1].ch[1] = 3; d.bus[1].nch = 2;
    d.nbuses = 2;
    return d;
}

static bool has(const bt_issue *v, size_t n, bt_issue_level lvl,
                const char *needle) {
    for (size_t i = 0; i < n; i++)
        if (v[i].level == lvl && strstr(v[i].msg, needle)) return true;
    return false;
}

static void expect(const bt_issue *v, size_t n, bt_issue_level lvl,
                   const char *needle) {
    bt_checks++;
    if (!has(v, n, lvl, needle)) {
        bt_fails++;
        fprintf(stderr, "  FAIL expected %s containing \"%s\"; got %zu issue(s):\n",
                bt_issue_level_name(lvl), needle, n);
        for (size_t i = 0; i < n; i++)
            fprintf(stderr, "        %s: %s\n",
                    bt_issue_level_name(v[i].level), v[i].msg);
    }
}

/* Validates a set list written inline; caller frees the issues. */
static bt_setlist *run(const char *json, bt_issue **v, size_t *n,
                       bt_setlist_stats *st, bool with_dev) {
    write_text("val_setlist.json", json);
    bt_setlist *sl = NULL;
    int line = 0;
    BT_CHECK_EQI(bt_setlist_load_file("val_setlist.json", &sl, &line), BT_OK);
    if (!sl) return NULL;

    bt_device_cfg d = mk_dev();
    BT_CHECK_EQI(bt_setlist_validate(sl, with_dev ? &d : NULL, SR, v, n, st), BT_OK);
    return sl;
}

static void cleanup(void) {
    remove("val_setlist.json");
    remove("val_good.wav"); remove("val_silent.wav");
    remove("val_hot.wav");  remove("val_441.wav");
}

/* ------------------------------------------------------------------ tests */

static void test_clean_set_is_clean(void) {
    write_tone("val_good.wav", SR, 0.5f, SR, 1);
    bt_issue *v = NULL; size_t n = 0; bt_setlist_stats st;
    bt_setlist *sl = run(
      "{\"version\":1,\"name\":\"ok\",\"songs\":[{\"title\":\"A\","
      "\"tempo\":{\"bpm\":120},\"tracks\":["
      "{\"type\":\"click\",\"bus\":\"inear\"},"
      "{\"name\":\"G\",\"type\":\"audio\",\"bus\":\"foh\",\"file\":\"val_good.wav\"}]}]}",
      &v, &n, &st, true);

    BT_CHECK_EQI(st.errors, 0);
    BT_CHECK_EQI(st.warnings, 0);
    BT_CHECK_EQI(n, 0);
    BT_CHECK_EQI(st.songs, 1);
    BT_CHECK_EQI(st.tracks, 2);
    BT_CHECK_NEAR(st.total_seconds, 1.0, 0.01);
    /* One mono second at 48k in float32. */
    BT_CHECK_EQI(st.peak_resident_bytes, (long long)(SR * 4));

    free(v); bt_setlist_free(sl); cleanup();
}

static void test_missing_file_is_an_error(void) {
    bt_issue *v = NULL; size_t n = 0; bt_setlist_stats st;
    bt_setlist *sl = run(
      "{\"version\":1,\"name\":\"x\",\"songs\":[{\"title\":\"A\","
      "\"tempo\":{\"bpm\":120},\"tracks\":["
      "{\"type\":\"click\",\"bus\":\"inear\"},"
      "{\"name\":\"G\",\"type\":\"audio\",\"bus\":\"foh\",\"file\":\"nope.wav\"}]}]}",
      &v, &n, &st, true);
    expect(v, n, BT_ISSUE_ERROR, "nope.wav");
    BT_CHECK(st.errors >= 1);
    free(v); bt_setlist_free(sl); cleanup();
}

static void test_unknown_bus_is_an_error(void) {
    write_tone("val_good.wav", SR / 2, 0.5f, SR, 1);
    bt_issue *v = NULL; size_t n = 0; bt_setlist_stats st;
    bt_setlist *sl = run(
      "{\"version\":1,\"name\":\"x\",\"songs\":[{\"title\":\"A\","
      "\"tempo\":{\"bpm\":120},\"tracks\":["
      "{\"type\":\"click\",\"bus\":\"inear\"},"
      "{\"name\":\"G\",\"type\":\"audio\",\"bus\":\"wedge3\",\"file\":\"val_good.wav\"}]}]}",
      &v, &n, &st, true);
    expect(v, n, BT_ISSUE_ERROR, "wedge3");

    /* Without a device.json there is nothing to check bus names against, and
     * the checker should say so by staying quiet rather than guessing. */
    free(v); v = NULL; n = 0;
    bt_setlist_free(sl);
    sl = run(
      "{\"version\":1,\"name\":\"x\",\"songs\":[{\"title\":\"A\","
      "\"tempo\":{\"bpm\":120},\"tracks\":["
      "{\"type\":\"click\",\"bus\":\"inear\"},"
      "{\"name\":\"G\",\"type\":\"audio\",\"bus\":\"wedge3\",\"file\":\"val_good.wav\"}]}]}",
      &v, &n, &st, false);
    BT_CHECK(!has(v, n, BT_ISSUE_ERROR, "wedge3"));

    free(v); bt_setlist_free(sl); cleanup();
}

static void test_silent_and_clipped_stems(void) {
    write_tone("val_silent.wav", SR / 2, 0.0f, SR, 1);
    write_tone("val_hot.wav",    SR / 2, 1.0f, SR, 1);
    bt_issue *v = NULL; size_t n = 0; bt_setlist_stats st;
    bt_setlist *sl = run(
      "{\"version\":1,\"name\":\"x\",\"songs\":[{\"title\":\"A\","
      "\"tempo\":{\"bpm\":120},\"tracks\":["
      "{\"type\":\"click\",\"bus\":\"inear\"},"
      "{\"name\":\"V\",\"type\":\"audio\",\"bus\":\"foh\",\"file\":\"val_silent.wav\"},"
      "{\"name\":\"L\",\"type\":\"audio\",\"bus\":\"foh\",\"file\":\"val_hot.wav\"}]}]}",
      &v, &n, &st, true);
    expect(v, n, BT_ISSUE_WARN, "entirely silent");
    expect(v, n, BT_ISSUE_WARN, "full scale");
    BT_CHECK_EQI(st.errors, 0);        /* both still play */
    free(v); bt_setlist_free(sl); cleanup();
}

static void test_rate_mismatch_is_announced(void) {
    write_tone("val_441.wav", 44100, 0.5f, 44100, 1);
    bt_issue *v = NULL; size_t n = 0; bt_setlist_stats st;
    bt_setlist *sl = run(
      "{\"version\":1,\"name\":\"x\",\"songs\":[{\"title\":\"A\","
      "\"tempo\":{\"bpm\":120},\"tracks\":["
      "{\"type\":\"click\",\"bus\":\"inear\"},"
      "{\"name\":\"G\",\"type\":\"audio\",\"bus\":\"foh\",\"file\":\"val_441.wav\"}]}]}",
      &v, &n, &st, true);
    expect(v, n, BT_ISSUE_WARN, "44100 Hz and will be resampled");
    /* Resident size is reported at the device rate, not the file's. */
    BT_CHECK_EQI(st.peak_resident_bytes, (long long)(SR * 4));
    free(v); bt_setlist_free(sl); cleanup();
}

static void test_offset_past_the_stem(void) {
    write_tone("val_good.wav", SR / 2, 0.5f, SR, 1);   /* half a second */
    bt_issue *v = NULL; size_t n = 0; bt_setlist_stats st;
    bt_setlist *sl = run(
      "{\"version\":1,\"name\":\"x\",\"songs\":[{\"title\":\"A\","
      "\"tempo\":{\"bpm\":120},\"tracks\":["
      "{\"type\":\"click\",\"bus\":\"inear\"},"
      "{\"name\":\"G\",\"type\":\"audio\",\"bus\":\"foh\",\"file\":\"val_good.wav\","
      "\"offset_ms\":-2000}]}]}",
      &v, &n, &st, true);
    expect(v, n, BT_ISSUE_WARN, "entirely before the song");
    free(v); bt_setlist_free(sl); cleanup();
}

static void test_structural_warnings(void) {
    write_tone("val_good.wav", SR / 2, 0.5f, SR, 1);
    bt_issue *v = NULL; size_t n = 0; bt_setlist_stats st;
    bt_setlist *sl = run(
      "{\"version\":1,\"name\":\"x\",\"songs\":["
      "{\"title\":\"Same\",\"tempo\":{\"bpm\":120},\"tracks\":["
      "{\"type\":\"click\",\"bus\":\"inear\"},"
      "{\"name\":\"G\",\"type\":\"audio\",\"bus\":\"foh\",\"file\":\"val_good.wav\"}]},"
      "{\"title\":\"Same\",\"on_end\":\"next\",\"tempo\":{\"bpm\":120},\"tracks\":["
      "{\"name\":\"G\",\"type\":\"audio\",\"bus\":\"foh\",\"file\":\"val_good.wav\"}]}]}",
      &v, &n, &st, true);
    expect(v, n, BT_ISSUE_WARN, "duplicate title");
    expect(v, n, BT_ISSUE_WARN, "no click track");
    expect(v, n, BT_ISSUE_WARN, "last song");
    free(v); bt_setlist_free(sl); cleanup();
}

static void test_preload_peak_is_the_largest_adjacent_pair(void) {
    /* Three songs of 1, 3 and 2 seconds. The window holds two adjacent
     * songs, so the peak is 1+3 or 3+2 - five seconds, not double the
     * largest and not the whole set. */
    write_tone("val_good.wav",   SR,     0.5f, SR, 1);
    write_tone("val_hot.wav",    SR * 3, 0.5f, SR, 1);
    write_tone("val_silent.wav", SR * 2, 0.5f, SR, 1);

    bt_issue *v = NULL; size_t n = 0; bt_setlist_stats st;
    bt_setlist *sl = run(
      "{\"version\":1,\"name\":\"x\",\"songs\":["
      "{\"title\":\"A\",\"tempo\":{\"bpm\":120},\"tracks\":["
      "{\"type\":\"click\",\"bus\":\"inear\"},"
      "{\"name\":\"G\",\"type\":\"audio\",\"bus\":\"foh\",\"file\":\"val_good.wav\"}]},"
      "{\"title\":\"B\",\"tempo\":{\"bpm\":120},\"tracks\":["
      "{\"type\":\"click\",\"bus\":\"inear\"},"
      "{\"name\":\"G\",\"type\":\"audio\",\"bus\":\"foh\",\"file\":\"val_hot.wav\"}]},"
      "{\"title\":\"C\",\"tempo\":{\"bpm\":120},\"tracks\":["
      "{\"type\":\"click\",\"bus\":\"inear\"},"
      "{\"name\":\"G\",\"type\":\"audio\",\"bus\":\"foh\",\"file\":\"val_silent.wav\"}]}]}",
      &v, &n, &st, true);

    BT_CHECK_EQI(st.peak_resident_bytes, (long long)(SR * 5 * 4));
    BT_CHECK_EQI(st.all_resident_bytes,  (long long)(SR * 6 * 4));
    BT_CHECK_NEAR(st.longest_seconds, 3.0, 0.01);
    BT_CHECK_NEAR(st.total_seconds,   6.0, 0.01);
    free(v); bt_setlist_free(sl); cleanup();
}

static void test_empty_and_bad_arguments(void) {
    bt_issue *v = NULL; size_t n = 0; bt_setlist_stats st;
    bt_setlist *sl = run("{\"version\":1,\"name\":\"x\",\"songs\":[]}",
                         &v, &n, &st, true);
    expect(v, n, BT_ISSUE_WARN, "no songs");
    BT_CHECK_EQI(st.peak_resident_bytes, 0);
    free(v); bt_setlist_free(sl);

    BT_CHECK(bt_setlist_validate(NULL, NULL, SR, &v, &n, &st) != BT_OK);
    BT_CHECK(bt_issue_level_name(BT_ISSUE_ERROR) != NULL);
    BT_CHECK(bt_issue_level_name(BT_ISSUE_WARN)  != NULL);
    cleanup();
}

int main(void) {
    BT_RUN(test_clean_set_is_clean);
    BT_RUN(test_missing_file_is_an_error);
    BT_RUN(test_unknown_bus_is_an_error);
    BT_RUN(test_silent_and_clipped_stems);
    BT_RUN(test_rate_mismatch_is_announced);
    BT_RUN(test_offset_past_the_stem);
    BT_RUN(test_structural_warnings);
    BT_RUN(test_preload_peak_is_the_largest_adjacent_pair);
    BT_RUN(test_empty_and_bad_arguments);
    BT_REPORT();
}
