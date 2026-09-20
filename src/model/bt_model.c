/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Tempo mathematics and song bookkeeping.
 */
#include "backtrack/bt_model.h"
#include "backtrack/bt_audio.h"
#include "backtrack/bt_resample.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

static bt_frame ms_to_frames(double ms, int32_t sr) {
    return (bt_frame)llround(ms * (double)sr / 1000.0);
}

/* ------------------------------------------------------------------ tempo */

bt_frame bt_tempo_beat_frame(const bt_tempo_map *tm, int64_t beat, int32_t sample_rate) {
    if (!tm || tm->nseg <= 0 || sample_rate <= 0) return 0;

    const bt_tempo_seg *s0 = &tm->seg[0];
    double sec;

    if (beat <= s0->start_beat) {
        /* Before the first tempo mark, including the count-in, which lives at
         * negative beat indices. */
        sec = (double)(beat - s0->start_beat) * 60.0 / s0->bpm;
    } else {
        sec = 0.0;
        for (int32_t i = 0; i < tm->nseg; i++) {
            int64_t from = tm->seg[i].start_beat;
            int64_t to   = (i + 1 < tm->nseg) ? tm->seg[i + 1].start_beat : beat;
            if (to > beat) to = beat;
            sec += (double)(to - from) * 60.0 / tm->seg[i].bpm;
            if (to == beat) break;
        }
    }
    /* One rounding, applied to the absolute position. Accumulating a per-beat
     * delta here is the classic way to drift half a beat across a long set. */
    return ms_to_frames(tm->downbeat_ms, sample_rate)
         + (bt_frame)llround(sec * (double)sample_rate);
}

int64_t bt_tempo_frame_beat(const bt_tempo_map *tm, bt_frame f, int32_t sample_rate) {
    if (!tm || tm->nseg <= 0 || sample_rate <= 0) return 0;

    double sec = (double)(f - ms_to_frames(tm->downbeat_ms, sample_rate))
               / (double)sample_rate;
    const bt_tempo_seg *s0 = &tm->seg[0];
    int64_t b;

    if (sec <= 0.0) {
        b = s0->start_beat + (int64_t)floor(sec * s0->bpm / 60.0);
    } else {
        b = tm->seg[tm->nseg - 1].start_beat;
        double acc = 0.0;
        for (int32_t i = 0; i < tm->nseg; i++) {
            int64_t st   = tm->seg[i].start_beat;
            bool    last = (i + 1 >= tm->nseg);
            double  dur  = last ? 0.0
                                : (double)(tm->seg[i + 1].start_beat - st) * 60.0
                                  / tm->seg[i].bpm;
            if (last || sec < acc + dur) {
                b = st + (int64_t)floor((sec - acc) * tm->seg[i].bpm / 60.0);
                break;
            }
            acc += dur;
        }
    }

    /* The estimate above inverts the *exact* tempo, but bt_tempo_beat_frame
     * rounds to the nearest sample. Half a sample of rounding is enough to
     * land this one beat early or late, so snap to the true inverse: the
     * largest beat whose frame is <= f. The estimate is never more than one
     * beat out, so the bounds are a guard, not a search. */
    for (int i = 0; i < 4 && bt_tempo_beat_frame(tm, b + 1, sample_rate) <= f; i++) b++;
    for (int i = 0; i < 4 && bt_tempo_beat_frame(tm, b, sample_rate) > f; i++) b--;
    return b;
}

bool bt_tempo_is_downbeat(const bt_tempo_map *tm, int64_t beat) {
    if (!tm || tm->sig_num <= 0) return true;
    int64_t n = tm->sig_num;
    int64_t m = beat % n;
    if (m < 0) m += n;            /* floored modulo: count-in beats are < 0 */
    return m == 0;
}

/* ------------------------------------------------------------------- song */

bt_frame bt_song_length(const bt_song *song, int32_t sample_rate) {
    if (!song) return 0;
    bt_frame end = 0;
    for (int32_t i = 0; i < song->ntracks; i++) {
        const bt_track *t = &song->track[i];
        if (t->type != BT_TRACK_AUDIO || !t->pcm) continue;
        bt_frame e = ms_to_frames((double)t->offset_ms, sample_rate) + t->frames;
        if (e > end) end = e;
    }
    return end;
}

size_t bt_song_pcm_bytes(const bt_song *song) {
    if (!song) return 0;
    size_t n = 0;
    for (int32_t i = 0; i < song->ntracks; i++) {
        const bt_track *t = &song->track[i];
        if (t->pcm) n += (size_t)t->channels * (size_t)t->frames * sizeof(float);
    }
    return n;
}

static void path_join(char *dst, size_t cap, const char *dir, const char *file) {
    if (!dir || !*dir) { snprintf(dst, cap, "%s", file); return; }
    size_t n = strlen(dir);
    const char *sep = (n && (dir[n - 1] == '/' || dir[n - 1] == '\\')) ? "" : "/";
    snprintf(dst, cap, "%s%s%s", dir, sep, file);
}

bt_err bt_song_load_audio(bt_song *song, const char *dir, int32_t sample_rate) {
    if (!song || sample_rate <= 0) return BT_ERR_RANGE;

    for (int32_t i = 0; i < song->ntracks; i++) {
        bt_track *t = &song->track[i];
        if (t->type != BT_TRACK_AUDIO) continue;
        if (t->pcm) continue;                     /* already resident */
        if (!t->file[0]) return BT_ERR_SCHEMA;

        char path[BT_MAX_PATH * 2];
        path_join(path, sizeof(path), dir, t->file);

        bt_audio w;
        bt_err e = bt_audio_decode_file(path, &w);
        if (e != BT_OK) { bt_song_free_audio(song); return e; }

        /* Stems arrive at whatever rate they were sold at - 44.1k and 48k in
         * the same set list is the normal case, not the exception. Convert
         * here, once, on the loader thread. Never in the callback. */
        const int32_t ch = w.channels;   /* bt_audio_free clears the struct */

        if (w.sample_rate != sample_rate) {
            float  **rs = NULL;
            bt_frame rn = 0;
            e = bt_resample_planar((const float *const *)w.pcm, ch,
                                   w.frames, w.sample_rate, sample_rate,
                                   &rs, &rn);
            bt_audio_free(&w);
            if (e != BT_OK) { bt_song_free_audio(song); return e; }
            t->pcm    = rs;
            t->frames = rn;
        } else {
            t->pcm    = w.pcm;
            t->frames = w.frames;
        }
        t->channels = ch;
    }
    return BT_OK;
}

void bt_song_free_audio(bt_song *song) {
    if (!song) return;
    for (int32_t i = 0; i < song->ntracks; i++) {
        bt_track *t = &song->track[i];
        if (!t->pcm) continue;
        for (int32_t c = 0; c < t->channels; c++) free(t->pcm[c]);
        free(t->pcm);
        t->pcm      = NULL;
        t->channels = 0;
        t->frames   = 0;
    }
}

void bt_setlist_free(bt_setlist *sl) {
    if (!sl) return;
    for (int32_t i = 0; i < sl->nsongs; i++) bt_song_free_audio(&sl->song[i]);
    free(sl->song);
    free(sl);
}

const bt_bus *bt_device_find_bus(const bt_device_cfg *cfg, const char *name) {
    if (!cfg || !name) return NULL;
    for (int32_t i = 0; i < cfg->nbuses; i++)
        if (strcmp(cfg->bus[i].name, name) == 0) return &cfg->bus[i];
    return NULL;
}

void bt_device_cfg_defaults(bt_device_cfg *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->sample_rate   = 48000;
    cfg->buffer_frames = 512;
    snprintf(cfg->device, sizeof(cfg->device), "%s", "default");

    /* The stereo fallback every bar band already owns: band mix left,
     * click right, split to two mono feeds at the desk. */
    snprintf(cfg->bus[0].name, BT_MAX_NAME, "%s", "foh");
    cfg->bus[0].ch[0] = 0; cfg->bus[0].nch = 1;
    snprintf(cfg->bus[1].name, BT_MAX_NAME, "%s", "inear");
    cfg->bus[1].ch[0] = 1; cfg->bus[1].nch = 1;
    cfg->nbuses = 2;
}
