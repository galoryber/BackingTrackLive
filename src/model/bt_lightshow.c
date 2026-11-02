/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Reads the lighting side's cue file. See include/backtrack/bt_lightshow.h
 * for whose file this is and why it is shaped the way it is.
 */
#include "backtrack/bt_lightshow.h"
#include "backtrack/bt_json.h"
#include "bt_write_util.h"

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
    free(show->special);
    free(show);
}

const bt_light_special *bt_lightshow_special(const bt_lightshow *show,
                                             const char *name) {
    if (!show || !name) return NULL;
    for (int32_t i = 0; i < show->nspecials; i++)
        if (!strcmp(show->special[i].name, name)) return &show->special[i];
    return NULL;
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

    /* Every special, by whatever name the file gives it, so one this
     * program has never heard of survives being written back out. */
    const bt_json *sp = bt_json_get(root, "specials");
    const size_t nsp = bt_json_keys(sp);
    if (nsp > 0) {
        s->special = (bt_light_special *)calloc(nsp, sizeof(*s->special));
        if (!s->special) { bt_lightshow_free(s); bt_json_free(root); return BT_ERR_ALLOC; }
    }
    for (size_t i = 0; i < nsp; i++) {
        const char *key = bt_json_key_at(sp, i);
        const bt_json *v = bt_json_value_at(sp, i);
        if (!key) continue;
        bt_light_special *o = &s->special[s->nspecials];
        snprintf(o->name, sizeof(o->name), "%s", key);
        o->ch   = (int32_t)bt_json_number(bt_json_get(v, "ch"),   0);
        o->note = (int32_t)bt_json_number(bt_json_get(v, "note"), -1);
        s->nspecials++;
    }

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

/* A song may hold a lot of cues, but not without bound: a runaway loop
 * adding them should hit a wall rather than eat the machine. */
#define BT_LIGHTSHOW_MAX_CUES 512

bt_err bt_lightshow_add_cue(bt_lightshow *show, const char *title,
                            bt_light_event **out) {
    if (out) *out = NULL;
    if (!show || !title || !*title) return BT_ERR_RANGE;

    bt_light_song *ls = bt_lightshow_song(show, title);
    if (!ls) {
        /* The show has never heard of this song. Give it an entry, which is
         * how a set list that started without a cue file gets one. */
        bt_light_song *grown = (bt_light_song *)realloc(
            show->song, (size_t)(show->nsongs + 1) * sizeof(*show->song));
        if (!grown) return BT_ERR_ALLOC;
        show->song = grown;
        ls = &show->song[show->nsongs];
        memset(ls, 0, sizeof(*ls));
        snprintf(ls->title, sizeof(ls->title), "%s", title);
        show->nsongs++;
    }

    if (ls->nev >= BT_LIGHTSHOW_MAX_CUES) return BT_ERR_RANGE;

    bt_light_event *ev = (bt_light_event *)realloc(
        ls->ev, (size_t)(ls->nev + 1) * sizeof(*ls->ev));
    if (!ev) return BT_ERR_ALLOC;
    ls->ev = ev;

    bt_light_event *c = &ls->ev[ls->nev];
    memset(c, 0, sizeof(*c));
    if (ls->nev > 0) {
        const bt_light_event *prev = &ls->ev[ls->nev - 1];
        c->bar  = prev->bar + 8;
        c->ch   = prev->ch;
        c->note = prev->note + 1 <= 127 ? prev->note + 1 : prev->note;
    } else {
        c->bar = 1; c->ch = 1; c->note = 0;
    }
    ls->nev++;
    if (out) *out = c;
    return BT_OK;
}

bt_err bt_lightshow_remove_cue(bt_lightshow *show, const char *title, int32_t at) {
    bt_light_song *ls = bt_lightshow_song(show, title);
    if (!ls || at < 0 || at >= ls->nev) return BT_ERR_RANGE;
    for (int32_t i = at; i + 1 < ls->nev; i++) ls->ev[i] = ls->ev[i + 1];
    ls->nev--;
    return BT_OK;
}

/* ---------------------------------------------------------------------------
 * Writing
 *
 * Laid out the way the lighting side's own generator lays it out, so a file
 * that has been through both is still readable as one thing by whoever has
 * both open. Written whole rather than patched: the format tag is versioned,
 * so anything this program does not model would arrive as a version it
 * refuses to load, rather than one it loads and silently strips.
 * ------------------------------------------------------------------------ */

static const char *kind_name(bt_light_kind k) {
    return k == BT_LIGHT_HIT ? "hit" : k == BT_LIGHT_END ? "end" : "look";
}

bt_err bt_lightshow_to_json(const bt_lightshow *show, char **out, size_t *len) {
    if (!show || !out) return BT_ERR_RANGE;
    *out = NULL;
    if (len) *len = 0;

    sb s;
    memset(&s, 0, sizeof(s));

    bt__sb_str(&s, "{\n  \"format\": ");
    bt__sb_json_string(&s, BT_LIGHTSHOW_FORMAT);
    bt__sb_str(&s, ",\n  \"midi_port\": ");
    bt__sb_json_string(&s, show->port);
    bt__sb_fmt(&s, ",\n  \"velocity\": %d", show->velocity);

    if (show->nspecials > 0) {
        bt__sb_str(&s, ",\n  \"specials\": {\n");
        for (int32_t i = 0; i < show->nspecials; i++) {
            const bt_light_special *sp = &show->special[i];
            bt__sb_str(&s, "    ");
            bt__sb_json_string(&s, sp->name);
            bt__sb_fmt(&s, ": { \"ch\": %d, \"note\": %d }", sp->ch, sp->note);
            bt__sb_str(&s, i + 1 < show->nspecials ? ",\n" : "\n");
        }
        bt__sb_str(&s, "  }");
    }

    bt__sb_str(&s, ",\n  \"songs\": [\n");
    for (int32_t i = 0; i < show->nsongs; i++) {
        const bt_light_song *ls = &show->song[i];
        bt__sb_str(&s, "    { \"title\": ");
        bt__sb_json_string(&s, ls->title);
        bt__sb_str(&s, ",\n      \"events\": [\n");
        for (int32_t k = 0; k < ls->nev; k++) {
            const bt_light_event *e = &ls->ev[k];
            bt__sb_fmt(&s, "        { \"bar\": %d, \"ch\": %d, \"note\": %d, \"type\": \"%s\"",
                       e->bar, e->ch, e->note, kind_name(e->kind));
            if (e->label[0]) {
                bt__sb_str(&s, ", \"label\": ");
                bt__sb_json_string(&s, e->label);
            }
            bt__sb_str(&s, " }");
            bt__sb_str(&s, k + 1 < ls->nev ? ",\n" : "\n");
        }
        bt__sb_str(&s, "      ] }");
        bt__sb_str(&s, i + 1 < show->nsongs ? ",\n" : "\n");
    }
    bt__sb_str(&s, "  ]\n}\n");

    if (s.failed) { free(s.buf); return BT_ERR_ALLOC; }
    *out = s.buf;
    if (len) *len = s.len;
    return BT_OK;
}

bt_err bt_lightshow_save_file(const bt_lightshow *show, const char *path) {
    if (!show || !path) return BT_ERR_RANGE;
    char  *text = NULL;
    size_t len  = 0;
    bt_err e = bt_lightshow_to_json(show, &text, &len);
    if (e != BT_OK) return e;
    e = bt__write_atomic(path, text, len);
    free(text);
    return e;
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

bt_light_song *bt_lightshow_song(bt_lightshow *show, const char *title) {
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
