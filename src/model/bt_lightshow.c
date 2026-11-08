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

    /* The format tag first. Everything below trusts the shape it promises.
     *
     * Two versions are accepted. They differ in one way - version 2 allows a
     * cue inside the count-in - and a version 1 file is a version 2 file that
     * happens not to use it, so there is nothing to translate. Refusing the
     * older tag would have taken a working rig down at the next launch over a
     * feature that file does not contain. */
    const char *fmt = bt_json_string(bt_json_get(root, "format"), "");
    if (!fmt || (strcmp(fmt, BT_LIGHTSHOW_FORMAT) != 0 &&
                 strcmp(fmt, BT_LIGHTSHOW_FORMAT_V1) != 0)) {
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
            if (ev->bar < BT_LIGHT_MIN_BAR || ev->ch < 1 || ev->ch > 16 ||
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

/* This song's entry, created if the show has never heard of it - which is how
 * a set list that started without a cue file gets one. NULL only on an
 * allocation failure, so a caller may treat it as BT_ERR_ALLOC. */
static bt_light_song *song_entry(bt_lightshow *show, const char *title) {
    bt_light_song *ls = bt_lightshow_song(show, title);
    if (ls) return ls;

    bt_light_song *grown = (bt_light_song *)realloc(
        show->song, (size_t)(show->nsongs + 1) * sizeof(*show->song));
    if (!grown) return NULL;
    show->song = grown;
    ls = &show->song[show->nsongs];
    memset(ls, 0, sizeof(*ls));
    snprintf(ls->title, sizeof(ls->title), "%s", title);
    show->nsongs++;
    return ls;
}

bt_err bt_lightshow_add_cue(bt_lightshow *show, const char *title,
                            bt_light_event **out) {
    if (out) *out = NULL;
    if (!show || !title || !*title) return BT_ERR_RANGE;

    bt_light_song *ls = song_entry(show, title);
    if (!ls) return BT_ERR_ALLOC;

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

bt_err bt_lightshow_copy_song(bt_lightshow *show, const char *from,
                              const char *to) {
    if (!show || !from || !*from || !to || !*to) return BT_ERR_RANGE;
    if (!strcmp(from, to)) return BT_OK;

    const bt_light_song *src = bt_lightshow_find(show, from);
    const int32_t n = src ? src->nev : 0;
    if (n > BT_LIGHTSHOW_MAX_CUES) return BT_ERR_RANGE;

    /* Nothing to copy and nothing to clear: say so rather than writing an
     * empty entry into a file the lighting side also reads. Copying a song
     * that has no lighting should leave no trace at all. */
    if (n == 0 && !bt_lightshow_find(show, to)) return BT_OK;

    /* The copy is taken before the destination entry is created, because
     * creating it may move the song array and leave `src` dangling. */
    bt_light_event *ev = NULL;
    if (n > 0) {
        ev = (bt_light_event *)malloc((size_t)n * sizeof(*ev));
        if (!ev) return BT_ERR_ALLOC;
        memcpy(ev, src->ev, (size_t)n * sizeof(*ev));
    }

    bt_light_song *dst = song_entry(show, to);
    if (!dst) { free(ev); return BT_ERR_ALLOC; }

    free(dst->ev);
    dst->ev  = ev;
    dst->nev = n;
    return BT_OK;
}

int32_t bt_lightshow_sort_song(bt_lightshow *show, const char *title,
                               int32_t track) {
    bt_light_song *ls = bt_lightshow_song(show, title);
    if (!ls || ls->nev < 2) return track;

    /* Insertion sort, stable, on an array that is already nearly sorted - a
     * generated show arrives in bar order and an edit moves one cue. A
     * sorting network would be the wrong shape for twenty events and would
     * not be stable, which matters: two looks on one downbeat were put in
     * that order for a reason. */
    for (int32_t i = 1; i < ls->nev; i++) {
        const bt_light_event held = ls->ev[i];
        int32_t j = i;
        while (j > 0 && ls->ev[j - 1].bar > held.bar) {
            ls->ev[j] = ls->ev[j - 1];
            if (track == j - 1)      track = j;
            else if (track == j)     track = j - 1;
            j--;
        }
        ls->ev[j] = held;
    }
    return track;
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

    /* The lowest version that can express this show, so a file the lighting
     * side's generator could already read keeps a tag it already accepts.
     * Only a count-in cue needs version 2, and only then does an older
     * reader need to refuse the file rather than drop the cue. */
    bool needs_v2 = false;
    for (int32_t i = 0; i < show->nsongs && !needs_v2; i++)
        for (int32_t k = 0; k < show->song[i].nev; k++)
            if (show->song[i].ev[k].bar < 1) { needs_v2 = true; break; }

    bt__sb_str(&s, "{\n  \"format\": ");
    bt__sb_json_string(&s, needs_v2 ? BT_LIGHTSHOW_FORMAT
                                    : BT_LIGHTSHOW_FORMAT_V1);
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

int32_t bt_light_bar_at(const bt_song *song, bt_frame at, int32_t sample_rate) {
    if (!song || sample_rate <= 0) return 1;
    const int32_t sig = song->tempo.sig_num > 0 ? song->tempo.sig_num : 4;
    const int64_t beat = bt_tempo_frame_beat(&song->tempo, at, sample_rate);

    /* Floor division. For a negative beat - the count-in - truncation would
     * round towards zero, which is up, and every count-in tap would land on
     * bar 1 instead of the count-in bar it was played in. */
    const int64_t bar = beat >= 0 ? beat / sig
                                  : -(((-beat) + sig - 1) / sig);
    const int64_t out = bar + 1;
    if (out < BT_LIGHT_MIN_BAR) return BT_LIGHT_MIN_BAR;
    if (out > 100000) return 100000;
    return (int32_t)out;
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
