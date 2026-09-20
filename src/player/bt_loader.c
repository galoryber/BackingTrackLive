/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#include "backtrack/bt_loader.h"
#include "backtrack/bt_thread.h"

#include <stdlib.h>
#include <string.h>

struct bt_loader {
    bt_setlist *sl;          /* borrowed */
    bt_engine  *eng;         /* borrowed, may be NULL */
    int32_t     sample_rate;

    bt_thread  *thread;
    bt_mutex   *mu;
    bt_cond    *work;        /* signalled when the window changes / quit    */
    bt_cond    *done;        /* signalled when a load or free completes     */

    /* All of the following are guarded by `mu`. */
    bool       *resident;
    int32_t     lo, hi;      /* desired window; lo > hi means "nothing"     */
    bool        quit;
    uint64_t    generation;
    bt_err      last_err;
    int32_t     last_err_song;
};

/* Picks one unit of work inside the window, nearest-first: the song the user
 * is most likely to need next should not wait behind the one after it. */
static int32_t pick_load(const bt_loader *l) {
    if (l->lo > l->hi) return -1;
    for (int32_t i = l->lo; i <= l->hi && i < l->sl->nsongs; i++)
        if (i >= 0 && !l->resident[i]) return i;
    return -1;
}

static int32_t pick_free(const bt_loader *l) {
    for (int32_t i = 0; i < l->sl->nsongs; i++)
        if (l->resident[i] && (i < l->lo || i > l->hi)) return i;
    return -1;
}

static void loader_main(void *user) {
    bt_loader *l = (bt_loader *)user;

    bt_mutex_lock(l->mu);
    for (;;) {
        if (l->quit) break;

        int32_t to_free = pick_free(l);
        if (to_free >= 0) {
            bt_song *s = &l->sl->song[to_free];
            l->resident[to_free] = false;
            bt_mutex_unlock(l->mu);

            /* Never free PCM a render could still be walking. */
            if (l->eng) bt_engine_sync(l->eng);
            bt_song_free_audio(s);

            bt_mutex_lock(l->mu);
            l->generation++;
            bt_cond_broadcast(l->done);
            continue;
        }

        int32_t to_load = pick_load(l);
        if (to_load >= 0) {
            bt_song *s   = &l->sl->song[to_load];
            const char *dir = l->sl->dir;
            int32_t     sr  = l->sample_rate;
            bt_mutex_unlock(l->mu);

            bt_err e = bt_song_load_audio(s, dir, sr);

            bt_mutex_lock(l->mu);
            if (e == BT_OK) {
                l->resident[to_load] = true;
            } else {
                /* A broken stem three songs ahead must surface without
                 * stopping the show. Mark it resident so the loader does not
                 * spin retrying it; selecting it will fail with this error. */
                l->resident[to_load]  = true;
                l->last_err           = e;
                l->last_err_song      = to_load;
            }
            l->generation++;
            bt_cond_broadcast(l->done);
            continue;
        }

        /* Nothing to do until the window moves. */
        bt_cond_wait(l->work, l->mu);
    }
    bt_mutex_unlock(l->mu);
}

bt_err bt_loader_start(bt_setlist *sl, int32_t sample_rate, bt_engine *eng,
                       bt_loader **out) {
    if (!sl || !out || sample_rate <= 0) return BT_ERR_RANGE;

    bt_loader *l = (bt_loader *)calloc(1, sizeof(*l));
    if (!l) return BT_ERR_ALLOC;
    l->sl          = sl;
    l->eng         = eng;
    l->sample_rate = sample_rate;
    l->lo          = 0;
    l->hi          = -1;            /* empty window until told otherwise */
    l->last_err    = BT_OK;
    l->last_err_song = -1;

    if (sl->nsongs > 0) {
        l->resident = (bool *)calloc((size_t)sl->nsongs, sizeof(bool));
        if (!l->resident) { free(l); return BT_ERR_ALLOC; }
        /* A click-only song has nothing to load and is resident already. */
        for (int32_t i = 0; i < sl->nsongs; i++) {
            bool needs = false;
            for (int32_t k = 0; k < sl->song[i].ntracks; k++)
                if (sl->song[i].track[k].type == BT_TRACK_AUDIO) needs = true;
            if (!needs) l->resident[i] = true;
        }
    }

    bt_err e;
    if ((e = bt_mutex_create(&l->mu))   != BT_OK) goto fail;
    if ((e = bt_cond_create(&l->work))  != BT_OK) goto fail;
    if ((e = bt_cond_create(&l->done))  != BT_OK) goto fail;
    if ((e = bt_thread_start(&l->thread, loader_main, l)) != BT_OK) goto fail;

    *out = l;
    return BT_OK;

fail:
    bt_cond_destroy(l->done);
    bt_cond_destroy(l->work);
    bt_mutex_destroy(l->mu);
    free(l->resident);
    free(l);
    return e;
}

void bt_loader_stop(bt_loader *l) {
    if (!l) return;

    bt_mutex_lock(l->mu);
    l->quit = true;
    bt_cond_broadcast(l->work);
    bt_mutex_unlock(l->mu);

    bt_thread_join(l->thread);

    /* The thread is gone, so no synchronisation is needed from here. */
    if (l->eng) bt_engine_sync(l->eng);
    for (int32_t i = 0; i < l->sl->nsongs; i++) bt_song_free_audio(&l->sl->song[i]);

    bt_cond_destroy(l->done);
    bt_cond_destroy(l->work);
    bt_mutex_destroy(l->mu);
    free(l->resident);
    free(l);
}

void bt_loader_set_window(bt_loader *l, int32_t lo, int32_t hi) {
    if (!l) return;
    bt_mutex_lock(l->mu);
    if (l->lo != lo || l->hi != hi) {
        l->lo = lo;
        l->hi = hi;
        bt_cond_broadcast(l->work);
    }
    bt_mutex_unlock(l->mu);
}

bool bt_loader_resident(bt_loader *l, int32_t song) {
    if (!l || song < 0 || song >= l->sl->nsongs) return false;
    bt_mutex_lock(l->mu);
    bool r = l->resident[song];
    bt_mutex_unlock(l->mu);
    return r;
}

bt_err bt_loader_wait(bt_loader *l, int32_t song, int32_t timeout_ms) {
    if (!l || song < 0 || song >= l->sl->nsongs) return BT_ERR_RANGE;

    bt_mutex_lock(l->mu);
    /* Make sure the song is actually wanted, or this would wait forever. */
    if (song < l->lo || song > l->hi) {
        l->lo = song;
        if (l->hi < song) l->hi = song;
        bt_cond_broadcast(l->work);
    }

    int32_t waited = 0;
    while (!l->resident[song] && !l->quit) {
        if (timeout_ms >= 0 && waited >= timeout_ms) {
            bt_mutex_unlock(l->mu);
            return BT_ERR_STATE;
        }
        bt_cond_wait_ms(l->done, l->mu, 20);
        waited += 20;
    }
    bt_err e = (l->last_err_song == song) ? l->last_err : BT_OK;
    bt_mutex_unlock(l->mu);
    return e;
}

size_t bt_loader_resident_bytes(bt_loader *l) {
    if (!l) return 0;
    bt_mutex_lock(l->mu);
    size_t n = 0;
    for (int32_t i = 0; i < l->sl->nsongs; i++)
        if (l->resident[i]) n += bt_song_pcm_bytes(&l->sl->song[i]);
    bt_mutex_unlock(l->mu);
    return n;
}

uint64_t bt_loader_generation(bt_loader *l) {
    if (!l) return 0;
    bt_mutex_lock(l->mu);
    uint64_t g = l->generation;
    bt_mutex_unlock(l->mu);
    return g;
}

bt_err bt_loader_last_error(bt_loader *l, int32_t *song) {
    if (!l) return BT_ERR_RANGE;
    bt_mutex_lock(l->mu);
    bt_err e = l->last_err;
    if (song) *song = l->last_err_song;
    bt_mutex_unlock(l->mu);
    return e;
}
