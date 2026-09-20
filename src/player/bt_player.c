/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#include "backtrack/bt_player.h"
#include "backtrack/bt_loader.h"

#include <stdlib.h>
#include <string.h>

struct bt_player {
    bt_player_cfg  cfg;
    bt_setlist    *sl;          /* borrowed */
    bt_device_cfg  dev;
    bt_engine     *eng;
    bt_loader     *ld;
    int32_t        current;     /* -1 when nothing is selected */
    bool           handled_end; /* end of the current song already acted on */
    uint64_t       seen_gen;    /* loader generation at the last tick       */
};

static void push_window(bt_player *p) {
    if (p->current < 0) { bt_loader_set_window(p->ld, 0, -1); return; }
    bt_loader_set_window(p->ld, p->current, p->current + p->cfg.preload_ahead);
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

    /* The loader is given the engine so it can drain in-flight renders before
     * freeing any stem's audio. */
    e = bt_loader_start(setlist, cfg->sample_rate, p->eng, &p->ld);
    if (e != BT_OK) { bt_engine_destroy(p->eng); free(p); return e; }

    *out = p;
    return BT_OK;
}

void bt_player_destroy(bt_player *p) {
    if (!p) return;
    /* The caller is expected to have stopped the device first, but draining
     * here too makes teardown safe rather than merely conventional. */
    bt_engine_stop(p->eng);
    bt_engine_sync(p->eng);
    /* Stopping the loader joins its thread and frees every stem it loaded.
     * The set list itself is borrowed and is not ours to free. */
    bt_loader_stop(p->ld);
    bt_engine_destroy(p->eng);
    free(p);
}

/* ------------------------------------------------------------- selection */

bt_err bt_player_select(bt_player *p, int32_t song_index) {
    if (!p) return BT_ERR_RANGE;
    if (song_index < 0 || song_index >= p->sl->nsongs) return BT_ERR_RANGE;

    bt_engine_stop(p->eng);

    /* Move the window first so the loader prioritises this song, then wait
     * for it. Inside the window - the normal case - it is already resident
     * and this returns immediately. Jumping across the set is the case that
     * actually waits, and waiting is the honest behaviour there: the user
     * asked for a song that is not in RAM. */
    p->current = song_index;
    push_window(p);

    bt_err e = bt_loader_wait(p->ld, song_index, p->cfg.load_timeout_ms);
    if (e != BT_OK) return e;

    e = bt_engine_set_song(p->eng, &p->sl->song[song_index], &p->dev);
    if (e != BT_OK) return e;

    p->handled_end = false;
    p->seen_gen    = bt_loader_generation(p->ld);
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

    /* Loading happens on the loader thread now; all this does is notice that
     * something finished, so a UI can redraw its residency display. */
    uint64_t gen = bt_loader_generation(p->ld);
    if (r == BT_TICK_IDLE && gen != p->seen_gen) r = BT_TICK_PRELOADED;
    p->seen_gen = gen;

    if (result) *result = r;
    return bt_loader_last_error(p->ld, NULL) == BT_OK ? BT_OK : BT_OK;
}

/* ------------------------------------------------------------- residency */

size_t bt_player_resident_bytes(bt_player *p) {
    return p ? bt_loader_resident_bytes(p->ld) : 0;
}

bool bt_player_song_resident(bt_player *p, int32_t song_index) {
    return p ? bt_loader_resident(p->ld, song_index) : false;
}

bt_err bt_player_wait_loaded(bt_player *p, int32_t song_index, int32_t timeout_ms) {
    if (!p) return BT_ERR_RANGE;
    return bt_loader_wait(p->ld, song_index, timeout_ms);
}

bt_err bt_player_load_error(bt_player *p, int32_t *song_index) {
    if (!p) return BT_ERR_RANGE;
    return bt_loader_last_error(p->ld, song_index);
}
