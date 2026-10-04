/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#include "backtrack/bt_demo.h"
#include "backtrack/bt_wav.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#define _USE_MATH_DEFINES
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------ demo set */

#ifdef _WIN32
  #include <direct.h>
  #define bt_mkdir(p) _mkdir(p)
#else
  #include <sys/stat.h>
  #include <sys/types.h>
  #define bt_mkdir(p) mkdir((p), 0777)
#endif

/* Create a directory, treating "it already exists" as success. Only one level
 * deep is needed here, and pretending otherwise would be more code than the
 * caller has use for. */
static bt_err make_dir(const char *path) {
    if (bt_mkdir(path) == 0) return BT_OK;
    return (errno == EEXIST) ? BT_OK : BT_ERR_IO;
}

static bt_err write_tone(const char *path, int32_t channels, double seconds,
                         int32_t rate, double hz, double amp) {
    bt_frame n = (bt_frame)(seconds * rate);
    float *buf[2] = { NULL, NULL };
    for (int32_t c = 0; c < channels; c++) {
        buf[c] = (float *)malloc((size_t)n * sizeof(float));
        if (!buf[c]) { for (int32_t k = 0; k < c; k++) free(buf[k]); return BT_ERR_ALLOC; }
        /* A slight detune per channel so a stereo stem is audibly stereo and
         * a routing mistake is obvious by ear. */
        double f = hz * (c ? 1.005 : 1.0);
        for (bt_frame i = 0; i < n; i++)
            buf[c][i] = (float)(amp * sin(2.0 * M_PI * f * (double)i / rate));
    }
    const float *p[2] = { buf[0], buf[1] };
    bt_err e = bt_wav_write_file(path, p, channels, rate, n);
    for (int32_t c = 0; c < channels; c++) free(buf[c]);
    return e;
}

static bt_err write_text_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) return BT_ERR_IO;
    size_t n = strlen(text);
    bool ok = fwrite(text, 1, n, f) == n;
    if (fclose(f) != 0) ok = false;
    return ok ? BT_OK : BT_ERR_IO;
}

bt_err bt_demo_write(const char *dir, double seconds,
                     bt_demo_progress progress, void *user) {
    char path[BT_MAX_PATH * 2];

    if (!dir || !*dir || seconds <= 0.0) return BT_ERR_RANGE;
    if (make_dir(dir) != BT_OK) return BT_ERR_IO;
    snprintf(path, sizeof(path), "%s/tracks", dir);
    if (make_dir(path) != BT_OK) return BT_ERR_IO;

    struct { const char *name; int32_t ch; double sec; int32_t rate; double hz, amp; }
    stems[] = {
        /* The second song's pad is 44.1 kHz on purpose: a real set list mixes
         * rates, and this makes the demo exercise load-time resampling. */
        { "tracks/synth.wav", 2, seconds,       48000, 440.0, 0.30 },
        { "tracks/bass.wav",  1, seconds,       48000, 110.0, 0.40 },
        { "tracks/pad.wav",   2, seconds * 0.75, 44100, 220.0, 0.25 },
    };

    for (size_t i = 0; i < sizeof(stems) / sizeof(stems[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, stems[i].name);
        bt_err e = write_tone(path, stems[i].ch, stems[i].sec, stems[i].rate,
                              stems[i].hz, stems[i].amp);
        if (e != BT_OK) return e;
        if (progress) progress(stems[i].name, user);
    }

    snprintf(path, sizeof(path), "%s/setlist.json", dir);
    bt_err e = write_text_file(path,
"{\n"
"  \"version\": 1,\n"
"  \"name\": \"Demo Set\",\n"
"  \"songs\": [\n"
"    {\n"
"      \"title\": \"Demo One\", \"artist\": \"BackingTrackLive\",\n"
"      \"tempo\": { \"bpm\": 120.0, \"sig\": [4, 4], \"downbeat_ms\": 0 },\n"
"      \"tuning\": \"E0\",\n"
"      \"count_in_bars\": 1, \"on_end\": \"next\",\n"
"      \"tracks\": [\n"
"        { \"name\": \"Click\", \"type\": \"click\", \"bus\": \"inear\" },\n"
"        { \"name\": \"Synth\", \"type\": \"audio\", \"bus\": \"foh\",\n"
"          \"file\": \"tracks/synth.wav\", \"gain_db\": -2.0, \"offset_ms\": 0 },\n"
"        { \"name\": \"Bass\",  \"type\": \"audio\", \"bus\": \"foh\",\n"
"          \"file\": \"tracks/bass.wav\", \"gain_db\": 0.0, \"offset_ms\": 0 }\n"
"      ]\n"
"    },\n"
"    {\n"
"      \"title\": \"Demo Two\", \"artist\": \"BackingTrackLive\",\n"
"      \"tempo\": { \"bpm\": 96.0, \"sig\": [4, 4], \"downbeat_ms\": 0 },\n"
"      \"tuning\": \"D1\",\n"
"      \"count_in_bars\": 2, \"on_end\": \"stop\",\n"
"      \"tracks\": [\n"
"        { \"name\": \"Click\", \"type\": \"click\", \"bus\": \"inear\" },\n"
"        { \"name\": \"Pad\",   \"type\": \"audio\", \"bus\": \"foh\",\n"
"          \"file\": \"tracks/pad.wav\", \"gain_db\": -4.0, \"offset_ms\": 0 }\n"
"      ]\n"
"    }\n"
"  ]\n"
"}\n");
    if (e != BT_OK) return e;
    if (progress) progress("setlist.json", user);

    /* Stereo by default because every interface has two outputs. The comment
     * in the file says how to make it four. */
    snprintf(path, sizeof(path), "%s/device.json", dir);
    e = write_text_file(path,
"{\n"
"  \"_comment\": [\n"
"    \"Edit this for THIS machine; it is never committed.\",\n"
"    \"Run  btplay --list-devices  to see what is present.\",\n"
"    \"Stereo by default: band mix left, click right, split at the desk.\",\n"
"    \"For four separate outputs, name the 4-channel endpoint and use\",\n"
"    \"  \\\"device\\\": \\\"OUT 1-4\\\", foh [0,1] and inear [2,3].\"\n"
"  ],\n"
"  \"device\": \"default\",\n"
"  \"api\": \"\",\n"
"  \"sample_rate\": 48000,\n"
"  \"buffer_frames\": 512,\n"
"  \"buses\": [\n"
"    { \"name\": \"foh\",   \"channels\": [0] },\n"
"    { \"name\": \"inear\", \"channels\": [1] }\n"
"  ]\n"
"}\n");
    if (e != BT_OK) return e;
    if (progress) progress("device.json", user);

    return BT_OK;
}
