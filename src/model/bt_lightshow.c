/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Reads the lighting side's cue file. See include/backtrack/bt_lightshow.h
 * for whose file this is and why it is shaped the way it is.
 */
#include "backtrack/bt_lightshow.h"
#include "backtrack/bt_json.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void copy_str(char *dst, size_t cap, const bt_json *j, const char *dflt) {
    const char *s = bt_json_string(j, dflt);
    if (!s) s = "";
    snprintf(dst, cap, "%s", s);
}

static bt_light_kind kind_of(const bt_json *j) {
    const char *s = bt_json_string(j, "look");
    if (s && !strcmp(s, "hit")) return BT_LIGHT_HIT;
    if (s && !strcmp(s, "end")) return BT_LIGHT_END;
    return BT_LIGHT_LOOK;   /* an unknown type is a look: it is the safe one */
}

static void read_special(bt_light_special *out, const bt_json *obj, const char *key) {
    const bt_json *j = bt_json_get(obj, key);
    out->ch   = (int32_t)bt_json_number(bt_json_get(j, "ch"),   0);
    out->note = (int32_t)bt_json_number(bt_json_get(j, "note"), -1);
}

static bt_err read_file(const char *path, char **out, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return BT_ERR_IO;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return BT_ERR_IO; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return BT_ERR_IO; }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return BT_ERR_ALLOC; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    *out = buf;
    *len = got;
    return BT_OK;
}

void bt_lightshow_free(bt_lightshow *show) {
    if (!show) return;
    for (int32_t i = 0; i < show->nsongs; i++) free(show->song[i].ev);
    free(show->song);
    free(show);
}

bt_err bt_lightshow_load_mem(const char *text, size_t len,
                             bt_lightshow **out, int *err_line) {
    if (!text || !out) return BT_ERR_RANGE;
    *out = NULL;

    bt_json *root = NULL;
    bt_err e = bt_json_parse(text, len, &root, err_line);
    if (e != BT_OK) return e;

    /* The format tag first. Everything below trusts the shape it promises. */
    const char *fmt = bt_json_string(bt_json_get(root, "format"), "");
    if (!fmt || strcmp(fmt, BT_LIGHTSHOW_FORMAT) != 0) {
        bt_json_free(root);
        return BT_ERR_SCHEMA;
    }

    bt_lightshow *s = (bt_lightshow *)calloc(1, sizeof(*s));
    if (!s) { bt_json_free(root); return BT_ERR_ALLOC; }

    copy_str(s->port, sizeof(s->port), bt_json_get(root, "midi_port"), "");
    s->velocity = (int32_t)bt_json_number(bt_json_get(root, "velocity"), 127);
    if (s->velocity < 1 || s->velocity > 127) s->velocity = 127;

    const bt_json *sp = bt_json_get(root, "specials");
    read_special(&s->between_songs, sp, "between_songs");
    read_special(&s->stop_all,      sp, "stop_all");
    read_special(&s->blinder,       sp, "blinder");
    read_special(&s->fog_left,      sp, "fog_left");
    read_special(&s->fog_right,     sp, "fog_right");
    read_special(&s->fog_dual,      sp, "fog_dual");

    const bt_json *songs = bt_json_get(root, "songs");
    const size_t n = bt_json_len(songs);
    if (n > 0) {
        s->song = (bt_light_song *)calloc(n, sizeof(*s->song));
        if (!s->song) { bt_lightshow_free(s); bt_json_free(root); return BT_ERR_ALLOC; }
    }

    for (size_t i = 0; i < n; i++) {
        const bt_json *sj = bt_json_at(songs, i);
        bt_light_song *ls = &s->song[s->nsongs];
        copy_str(ls->title, sizeof(ls->title), bt_json_get(sj, "title"), "");

        const bt_json *evs = bt_json_get(sj, "events");
        const size_t m = bt_json_len(evs);
        if (m > 0) {
            ls->ev = (bt_light_event *)calloc(m, sizeof(*ls->ev));
            if (!ls->ev) { bt_lightshow_free(s); bt_json_free(root); return BT_ERR_ALLOC; }
        }
        for (size_t k = 0; k < m; k++) {
            const bt_json *ej = bt_json_at(evs, k);
            bt_light_event *ev = &ls->ev[ls->nev];
            ev->bar  = (int32_t)bt_json_number(bt_json_get(ej, "bar"),  0);
            ev->ch   = (int32_t)bt_json_number(bt_json_get(ej, "ch"),   0);
            ev->note = (int32_t)bt_json_number(bt_json_get(ej, "note"), -1);
            ev->kind = kind_of(bt_json_get(ej, "type"));
            copy_str(ev->label, sizeof(ev->label), bt_json_get(ej, "label"), "");

            /* A nonsense event is dropped rather than failing the file. The
             * show is somebody else's build output; one bad row should cost
             * one look, not the whole rig. */
            if (ev->bar < 1 || ev->ch < 1 || ev->ch > 16 ||
                ev->note < 0 || ev->note > 127) continue;
            ls->nev++;
        }
        s->nsongs++;
    }

    bt_json_free(root);
    *out = s;
    return BT_OK;
}

bt_err bt_lightshow_load_file(const char *path, bt_lightshow **out, int *err_line) {
    if (!path || !out) return BT_ERR_RANGE;
    char *buf = NULL; size_t len = 0;
    bt_err e = read_file(path, &buf, &len);
    if (e != BT_OK) return e;
    e = bt_lightshow_load_mem(buf, len, out, err_line);
    free(buf);
    return e;
}

const bt_light_song *bt_lightshow_find(const bt_lightshow *show, const char *title) {
    if (!show || !title || !*title) return NULL;
    for (int32_t i = 0; i < show->nsongs; i++)
        if (!strcmp(show->song[i].title, title)) return &show->song[i];
    return NULL;
}

bt_frame bt_light_event_frame(const bt_song *song, const bt_light_event *ev,
                              int32_t sample_rate) {
    if (!song || !ev || sample_rate <= 0) return 0;
    const int32_t sig = song->tempo.sig_num > 0 ? song->tempo.sig_num : 4;
    /* Bar 1 is beat 0, which is the downbeat after the count-in. From the
     * beat index, never accumulated - a bar two hundred in has to land where
     * the click does. */
    const int64_t beat = (int64_t)(ev->bar - 1) * (int64_t)sig;
    return bt_tempo_beat_frame(&song->tempo, beat, sample_rate);
}

int32_t bt_light_events_between(const bt_light_song *ls, const bt_song *song,
                                bt_frame after, bt_frame upto,
                                int32_t sample_rate, int32_t *out, int32_t cap) {
    if (!ls || !song || !out || cap <= 0 || sample_rate <= 0) return 0;
    int32_t n = 0;
    for (int32_t i = 0; i < ls->nev && n < cap; i++) {
        const bt_frame f = bt_light_event_frame(song, &ls->ev[i], sample_rate);
        if (f > after && f <= upto) out[n++] = i;
    }
    return n;
}

int32_t bt_light_look_at(const bt_light_song *ls, const bt_song *song,
                         bt_frame at, int32_t sample_rate) {
    if (!ls || !song || sample_rate <= 0) return -1;
    int32_t best = -1;
    for (int32_t i = 0; i < ls->nev; i++) {
        /* Hits are moments and a moment that has passed has passed. An end is
         * not a look either: seeking back into a song should not leave the
         * between-songs state up. */
        if (ls->ev[i].kind != BT_LIGHT_LOOK) continue;
        if (bt_light_event_frame(song, &ls->ev[i], sample_rate) <= at) best = i;
    }
    return best;
}
