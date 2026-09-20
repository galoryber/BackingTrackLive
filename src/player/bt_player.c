/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#include "backtrack/bt_player.h"

#include <stdlib.h>
#include <string.h>

struct bt_player {
    bt_player_cfg  cfg;
    bt_setlist    *sl;          /* borrowed */
    bt_device_cfg  dev;
    bt_engine     *eng;
    int32_t        current;     /* -1 when nothing is selected */
    bool           handled_end; /* end of the current song already acted on */
};

/* Resident means every audio stem is in RAM. A click-only song has nothing to
 * load, so it is trivially resident. */
static bool song_resident(const bt_song *s) {
    for (int32_t i = 0; i < s->ntracks; i++)
        if (s->track[i].type == BT_TRACK_AUDIO && !s->track[i].pcm) return false;
    return true;
}

static bool in_preload_window(const bt_player *p, int32_t index) {
    if (p->current < 0) return false;
    return index >= p->current && index <= p->current + p->cfg.preload_ahead;
}

bt_err bt_player_create(const bt_player_cfg *cfg, bt_setlist *setlist,
                        const bt_device_cfg *dev, bt_player **out) {
    if (!cfg || !setlist || !dev || !out) return BT_ERR_RANGE;
    if (cfg->preload_ahead < 0) return BT_ERR_RANGE;

    bt_player *p = (bt_player *)calloc(1, sizeof(*p));
    if (!p) return BT_ERR_ALLOC;

    p->cfg     = *cfg;
    p->sl      = setlist;
    p->dev     = *dev;
    p->current = -1;

    bt_engine_cfg ec = { cfg->sample_rate, cfg->out_channels, cfg->max_block_frames };
    bt_err e = bt_engine_create(&ec, &p->eng);
    if (e != BT_OK) { free(p); return e; }

    *out = p;
    return BT_OK;
}

void bt_player_destroy(bt_player *p) {
    if (!p) return;
    /* Free the audio we loaded, but not the set list - it is borrowed. */
    for (int32_t i = 0; i < p->sl->nsongs; i++) bt_song_free_audio(&p->sl->song[i]);
    bt_engine_destroy(p->eng);
    free(p);
}

/* ------------------------------------------------------------- selection */

bt_err bt_player_select(bt_player *p, int32_t song_index) {
    if (!p) return BT_ERR_RANGE;
    if (song_index < 0 || song_index >= p->sl->nsongs) return BT_ERR_RANGE;

    bt_engine_stop(p->eng);

    bt_song *s = &p->sl->song[song_index];
    bt_err e = bt_song_load_audio(s, p->sl->dir, p->cfg.sample_rate);
    if (e != BT_OK) return e;

    e = bt_engine_set_song(p->eng, s, &p->dev);
    if (e != BT_OK) return e;

    p->current     = song_index;
    p->handled_end = false;
    return BT_OK;
}

bt_err bt_player_next(bt_player *p) {
    if (!p) return BT_ERR_RANGE;
    if (p->current + 1 >= p->sl->nsongs) return BT_ERR_RANGE;
    return bt_player_select(p, p->current + 1);
}

bt_err bt_player_prev(bt_player *p) {
    if (!p) return BT_ERR_RANGE;
    if (p->current <= 0) return BT_ERR_RANGE;
    return bt_player_select(p, p->current - 1);
}

int32_t bt_player_current(const bt_player *p) { return p ? p->current : -1; }
int32_t bt_player_count(const bt_player *p)   { return p ? p->sl->nsongs : 0; }

const bt_song *bt_player_song(const bt_player *p) {
    if (!p || p->current < 0) return NULL;
    return &p->sl->song[p->current];
}

/* ------------------------------------------------------------- transport */

void bt_player_start(bt_player *p) {
    if (!p || p->current < 0) return;
    p->handled_end = false;
    bt_engine_start_with_count_in(p->eng);
}

void bt_player_play(bt_player *p) {
    if (!p || p->current < 0) return;
    p->handled_end = false;
    bt_engine_play(p->eng);
}

void bt_player_stop(bt_player *p)   { if (p) bt_engine_stop(p->eng); }
void bt_player_panic(bt_player *p)  { if (p) bt_engine_panic(p->eng); }

bool bt_player_playing(const bt_player *p) {
    return p && bt_engine_playing(p->eng);
}

bt_frame bt_player_playhead(const bt_player *p) {
    return p ? bt_engine_playhead(p->eng) : 0;
}

void bt_player_render(bt_player *p, float *const *out, int32_t nframes) {
    if (!p) return;
    bt_engine_render(p->eng, out, nframes);
}

/* ------------------------------------------------------------------ tick */

/* Loads anything inside the preload window and frees anything outside it.
 * Reports whether it loaded something, so the UI can say so. */
static bt_err run_preload(bt_player *p, bool *loaded_something) {
    *loaded_something = false;
    bt_err first_err = BT_OK;

    for (int32_t i = 0; i < p->sl->nsongs; i++) {
        bt_song *s = &p->sl->song[i];
        bool want = in_preload_window(p, i);
        bool have = song_resident(s);

        if (want && !have) {
            bt_err e = bt_song_load_audio(s, p->sl->dir, p->cfg.sample_rate);
            if (e != BT_OK) {
                /* A broken stem three songs ahead must not stop the show. Keep
                 * going, report the first failure, and let the UI surface it
                 * now rather than when that song is selected mid-set. */
                if (first_err == BT_OK) first_err = e;
                continue;
            }
            *loaded_something = true;
        } else if (!want && have) {
            bt_song_free_audio(s);
        }
    }
    return first_err;
}

bt_err bt_player_tick(bt_player *p, bt_tick_result *result) {
    bt_tick_result r = BT_TICK_IDLE;
    if (result) *result = r;
    if (!p) return BT_ERR_RANGE;
    if (p->current < 0) return BT_OK;

    /* End of song. bt_engine stops itself on the sample; deciding what that
     * means is this layer's job, and it happens once. */
    if (bt_engine_finished(p->eng) && !p->handled_end) {
        p->handled_end = true;
        const bt_song *s = &p->sl->song[p->current];

        if (s->on_end == BT_ON_END_NEXT && p->current + 1 < p->sl->nsongs) {
            bt_err e = bt_player_select(p, p->current + 1);
            if (e != BT_OK) return e;
            /* No count-in on a segue: clicking a bar into the next song is
             * not what "plays straight into the next one" means. */
            bt_engine_seek(p->eng, 0);
            bt_engine_play(p->eng);
            r = BT_TICK_ADVANCED;
        } else {
            /* Either on_end: stop, or the last song - which is the same thing.
             * The singer is talking; wait for a human. */
            r = BT_TICK_SONG_ENDED;
        }
    }

    bool loaded = false;
    bt_err e = run_preload(p, &loaded);
    if (r == BT_TICK_IDLE && loaded) r = BT_TICK_PRELOADED;
    if (result) *result = r;
    return e;
}

/* ------------------------------------------------------------- residency */

size_t bt_player_resident_bytes(const bt_player *p) {
    if (!p) return 0;
    size_t n = 0;
    for (int32_t i = 0; i < p->sl->nsongs; i++)
        n += bt_song_pcm_bytes(&p->sl->song[i]);
    return n;
}

bool bt_player_song_resident(const bt_player *p, int32_t song_index) {
    if (!p || song_index < 0 || song_index >= p->sl->nsongs) return false;
    return song_resident(&p->sl->song[song_index]);
}
