/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#include "backtrack/bt_validate.h"
#include "backtrack/bt_audio.h"
#include "backtrack/bt_resample.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

typedef struct {
    bt_issue *v;
    size_t    n, cap;
} issues;

static void add(issues *is, bt_issue_level lvl, int32_t song, int32_t track,
                const char *fmt, ...) {
    if (is->n == is->cap) {
        size_t cap = is->cap ? is->cap * 2 : 32;
        bt_issue *q = (bt_issue *)realloc(is->v, cap * sizeof(*q));
        if (!q) return;                 /* drop the issue rather than fail */
        is->v = q;
        is->cap = cap;
    }
    bt_issue *it = &is->v[is->n];
    it->level = lvl;
    it->song  = song;
    it->track = track;

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(it->msg, sizeof(it->msg), fmt, ap);
    va_end(ap);
    is->n++;
}

const char *bt_issue_level_name(bt_issue_level l) {
    return l == BT_ISSUE_ERROR ? "error" : "warning";
}

static void path_join(char *dst, size_t cap, const char *dir, const char *file) {
    if (!dir || !*dir) { snprintf(dst, cap, "%s", file); return; }
    size_t n = strlen(dir);
    const char *sep = (n && (dir[n - 1] == '/' || dir[n - 1] == '\\')) ? "" : "/";
    snprintf(dst, cap, "%s%s%s", dir, sep, file);
}

bt_err bt_setlist_validate(bt_setlist *sl, const bt_device_cfg *dev,
                           int32_t sample_rate,
                           bt_issue **out, size_t *nout,
                           bt_setlist_stats *stats) {
    if (!sl || !out || !nout || sample_rate <= 0) return BT_ERR_RANGE;

    issues is;
    memset(&is, 0, sizeof(is));

    bt_setlist_stats st;
    memset(&st, 0, sizeof(st));
    st.songs = sl->nsongs;

    if (sl->nsongs == 0)
        add(&is, BT_ISSUE_WARN, -1, -1, "set list contains no songs");

    /* Per-song resident size, kept so the preload peak can be worked out
     * afterwards: the window holds two adjacent songs, so the peak is the
     * largest adjacent pair, not the largest song doubled. */
    size_t *song_bytes = NULL;
    if (sl->nsongs > 0) {
        song_bytes = (size_t *)calloc((size_t)sl->nsongs, sizeof(size_t));
        if (!song_bytes) { free(is.v); return BT_ERR_ALLOC; }
    }

    for (int32_t i = 0; i < sl->nsongs; i++) {
        bt_song *s = &sl->song[i];
        bool  has_click = false;
        double song_seconds = 0.0;

        if (!s->title[0] || strcmp(s->title, "Untitled") == 0)
            add(&is, BT_ISSUE_WARN, i, -1, "song has no title");

        for (int32_t j = 0; j < i; j++)
            if (strcmp(sl->song[j].title, s->title) == 0) {
                add(&is, BT_ISSUE_WARN, i, -1,
                    "duplicate title, same as song %d", j + 1);
                break;
            }

        for (int32_t t = 0; t < s->ntracks; t++) {
            bt_track *tr = &s->track[t];
            st.tracks++;

            if (dev && !bt_device_find_bus(dev, tr->bus))
                add(&is, BT_ISSUE_ERROR, i, t,
                    "routes to bus \"%s\", which device.json does not define",
                    tr->bus);

            if (tr->type == BT_TRACK_CLICK) { has_click = true; continue; }

            char path[BT_MAX_PATH * 2];
            path_join(path, sizeof(path), sl->dir, tr->file);

            bt_audio a;
            bt_err e = bt_audio_decode_file(path, &a);
            if (e != BT_OK) {
                add(&is, BT_ISSUE_ERROR, i, t, "%s: %s", tr->file, bt_strerror(e));
                continue;
            }

            /* Measure before resampling: peak and silence are properties of
             * the file, and saying "your stem is silent" is more useful than
             * anything the renderer could report later. */
            float peak = 0.0f;
            for (int32_t c = 0; c < a.channels; c++)
                for (bt_frame k = 0; k < a.frames; k++) {
                    float v = fabsf(a.pcm[c][k]);
                    if (v > peak) peak = v;
                }

            if (peak == 0.0f)
                add(&is, BT_ISSUE_WARN, i, t,
                    "%s is entirely silent - wrong file?", tr->file);
            else if (peak >= 0.999f)
                add(&is, BT_ISSUE_WARN, i, t,
                    "%s peaks at full scale and may already be clipped", tr->file);

            double secs = (double)a.frames / (double)a.sample_rate;
            if (secs > song_seconds) song_seconds = secs;

            if (a.sample_rate != sample_rate)
                add(&is, BT_ISSUE_WARN, i, t,
                    "%s is %d Hz and will be resampled to %d Hz at load",
                    tr->file, a.sample_rate, sample_rate);

            double off_s = (double)tr->offset_ms / 1000.0;
            if (off_s <= -secs)
                add(&is, BT_ISSUE_WARN, i, t,
                    "offset %d ms shifts %s entirely before the song starts",
                    tr->offset_ms, tr->file);

            /* Resident size is measured at the device rate, since that is
             * what will actually sit in RAM. */
            bt_frame at_rate = bt_resample_out_frames(a.frames, a.sample_rate,
                                                      sample_rate);
            song_bytes[i] += (size_t)a.channels * (size_t)at_rate * sizeof(float);

            bt_audio_free(&a);
        }

        if (!has_click)
            add(&is, BT_ISSUE_WARN, i, -1,
                "no click track - was that intended?");

        if (s->on_end == BT_ON_END_NEXT && i + 1 >= sl->nsongs)
            add(&is, BT_ISSUE_WARN, i, -1,
                "on_end is \"next\" but this is the last song; it will stop");

        st.total_seconds += song_seconds;
        if (song_seconds > st.longest_seconds) st.longest_seconds = song_seconds;
        st.all_resident_bytes += song_bytes[i];
    }

    /* The preload window holds the current song and the next one. */
    for (int32_t i = 0; i < sl->nsongs; i++) {
        size_t pair = song_bytes[i];
        if (i + 1 < sl->nsongs) pair += song_bytes[i + 1];
        if (pair > st.peak_resident_bytes) st.peak_resident_bytes = pair;
    }

    for (size_t k = 0; k < is.n; k++)
        if (is.v[k].level == BT_ISSUE_ERROR) st.errors++; else st.warnings++;

    free(song_bytes);
    *out   = is.v;
    *nout  = is.n;
    if (stats) *stats = st;
    return BT_OK;
}
