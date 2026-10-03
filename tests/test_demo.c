/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * The demo set the UI's start screen and btrender --make-demo both write.
 *
 * It used to live in tools/ where coverage never looked at it, which meant
 * the first thing a new user runs was the least tested thing in the tree.
 * What matters here is not that it writes some files: it is that what it
 * writes is a valid set list, because it is also the worked example people
 * copy when building their own.
 */
#include "bt_test.h"
#include "backtrack/bt_demo.h"
#include "backtrack/bt_validate.h"
#include "backtrack/bt_audio.h"

#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define rmrf_cmd "rmdir /s /q "
#else
#define rmrf_cmd "rm -rf "
#endif

static void scrub(const char *dir) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s\"%s\" 2>%s", rmrf_cmd, dir,
#ifdef _WIN32
             "NUL");
#else
             "/dev/null");
#endif
    int rc = system(cmd);
    (void)rc;
}

static bool exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int nprogress = 0;
static void count_progress(const char *what, void *user) {
    BT_CHECK(what != NULL && *what != '\0');
    BT_CHECK(user == &nprogress);
    nprogress++;
}

static void test_writes_a_valid_set(void) {
    const char *dir = "tmp_demo_valid";
    scrub(dir);

    /* Three seconds, not the two minutes the UI uses: this test is about
     * what gets written, and nothing here is length-dependent. */
    nprogress = 0;
    BT_CHECK(bt_demo_write(dir, 3.0, count_progress, &nprogress) == BT_OK);

    /* Three stems, a set list and a device map. */
    BT_CHECK(nprogress == 5);

    char path[512];
    snprintf(path, sizeof(path), "%s/setlist.json", dir);
    BT_CHECK(exists(path));
    snprintf(path, sizeof(path), "%s/device.json", dir);
    BT_CHECK(exists(path));
    snprintf(path, sizeof(path), "%s/tracks/synth.wav", dir);
    BT_CHECK(exists(path));

    /* The point of the whole test: load it the way the player would, and
     * check the checker finds nothing wrong with it. */
    bt_setlist *sl = NULL;
    int line = 0;
    snprintf(path, sizeof(path), "%s/setlist.json", dir);
    BT_CHECK(bt_setlist_load_file(path, &sl, &line) == BT_OK);
    BT_CHECK(sl != NULL);
    BT_CHECK(sl->nsongs == 2);

    bt_device_cfg dev;
    bt_device_cfg_defaults(&dev);
    snprintf(path, sizeof(path), "%s/device.json", dir);
    int dline = 0;
    BT_CHECK(bt_device_cfg_load_file(path, &dev, &dline) == BT_OK);
    BT_CHECK(dev.nbuses >= 2);

    bt_issue *issues = NULL;
    size_t nissues = 0;
    bt_setlist_stats stats;
    BT_CHECK(bt_setlist_validate(sl, &dev, 48000, &issues, &nissues, &stats) == BT_OK);
    BT_CHECK(stats.errors == 0);          /* a demo that does not check is a bug */
    BT_CHECK(stats.songs == 2);
    BT_CHECK(stats.tracks > 2);

    free(issues);
    bt_setlist_free(sl);
    scrub(dir);
}

/* Every song in the demo carries a click, and one segues into the next -
 * the two things the demo exists to show off. */
static void test_demonstrates_the_features(void) {
    const char *dir = "tmp_demo_features";
    scrub(dir);
    BT_CHECK(bt_demo_write(dir, 2.0, NULL, NULL) == BT_OK);   /* NULL progress is fine */

    char path[512];
    snprintf(path, sizeof(path), "%s/setlist.json", dir);
    bt_setlist *sl = NULL;
    int line = 0;
    BT_CHECK(bt_setlist_load_file(path, &sl, &line) == BT_OK);

    bool saw_segue = false;
    for (int32_t i = 0; i < sl->nsongs; i++) {
        bool has_click = false;
        for (int32_t t = 0; t < sl->song[i].ntracks; t++)
            if (sl->song[i].track[t].type == BT_TRACK_CLICK) has_click = true;
        BT_CHECK(has_click);
        BT_CHECK(sl->song[i].tempo.nseg >= 1);
        if (sl->song[i].on_end == BT_ON_END_NEXT) saw_segue = true;
    }
    BT_CHECK(saw_segue);

    bt_setlist_free(sl);
    scrub(dir);
}

/* Stems resample on load, so the demo deliberately ships one at 44100 against
 * a 48000 device. If that stopped being true the resampler would quietly go
 * untested by anyone who only ever runs the demo. */
static void test_mixed_sample_rates(void) {
    const char *dir = "tmp_demo_rates";
    scrub(dir);
    BT_CHECK(bt_demo_write(dir, 2.0, NULL, NULL) == BT_OK);

    char path[512];
    snprintf(path, sizeof(path), "%s/tracks/pad.wav", dir);
    bt_audio buf;
    BT_CHECK(bt_audio_decode_file(path, &buf) == BT_OK);
    BT_CHECK(buf.sample_rate == 44100);
    bt_audio_free(&buf);

    snprintf(path, sizeof(path), "%s/tracks/synth.wav", dir);
    BT_CHECK(bt_audio_decode_file(path, &buf) == BT_OK);
    BT_CHECK(buf.sample_rate == 48000);
    BT_CHECK(buf.channels == 2);
    bt_audio_free(&buf);

    scrub(dir);
}

static void test_rejects_nonsense(void) {
    BT_CHECK(bt_demo_write(NULL, 3.0, NULL, NULL) == BT_ERR_RANGE);
    BT_CHECK(bt_demo_write("", 3.0, NULL, NULL) == BT_ERR_RANGE);
    BT_CHECK(bt_demo_write("tmp_demo_bad", 0.0, NULL, NULL) == BT_ERR_RANGE);
    BT_CHECK(bt_demo_write("tmp_demo_bad", -1.0, NULL, NULL) == BT_ERR_RANGE);

    /* A directory that cannot be created, because its parent is not there. */
    BT_CHECK(bt_demo_write("tmp_demo_missing/deeper/still", 1.0, NULL, NULL) == BT_ERR_IO);
}

/* Writing twice over the top of an existing demo succeeds - people will do
 * this, and a half-overwritten set list would be worse than either outcome. */
static void test_rewrite_is_idempotent(void) {
    const char *dir = "tmp_demo_twice";
    scrub(dir);
    BT_CHECK(bt_demo_write(dir, 2.0, NULL, NULL) == BT_OK);
    BT_CHECK(bt_demo_write(dir, 2.0, NULL, NULL) == BT_OK);

    char path[512];
    snprintf(path, sizeof(path), "%s/setlist.json", dir);
    bt_setlist *sl = NULL;
    int line = 0;
    BT_CHECK(bt_setlist_load_file(path, &sl, &line) == BT_OK);
    BT_CHECK(sl->nsongs == 2);
    bt_setlist_free(sl);
    scrub(dir);
}

int main(void) {
    BT_RUN(test_writes_a_valid_set);
    BT_RUN(test_demonstrates_the_features);
    BT_RUN(test_mixed_sample_rates);
    BT_RUN(test_rejects_nonsense);
    BT_RUN(test_rewrite_is_idempotent);
    BT_REPORT();
}
