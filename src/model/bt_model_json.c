/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Binds the JSON tree onto the set list / device model, with range checks.
 *
 * Two files, deliberately separate:
 *   setlist.json  - portable, committed to git, paths relative to itself
 *   device.json   - machine-local bus -> channel map, never committed
 * That split is what lets the same set list folder run on the gig laptop and
 * the backup laptop with a different interface in it.
 */
#include "backtrack/bt_model.h"
#include "backtrack/bt_json.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define BT_SETLIST_VERSION 1

static void copy_str(char *dst, size_t cap, const bt_json *j, const char *dflt) {
    const char *s = bt_json_string(j, dflt);
    if (!s) s = "";
    snprintf(dst, cap, "%s", s);
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

/* ------------------------------------------------------------------ tempo */

static bt_err bind_tempo(const bt_json *jt, bt_tempo_map *tm) {
    memset(tm, 0, sizeof(*tm));
    tm->sig_num = 4;
    tm->sig_den = 4;

    const bt_json *sig = bt_json_get(jt, "sig");
    if (sig && bt_json_len(sig) == 2) {
        tm->sig_num = (int32_t)bt_json_number(bt_json_at(sig, 0), 4);
        tm->sig_den = (int32_t)bt_json_number(bt_json_at(sig, 1), 4);
    }
    if (tm->sig_num < 1 || tm->sig_num > 32) return BT_ERR_SCHEMA;
    if (tm->sig_den < 1 || tm->sig_den > 64) return BT_ERR_SCHEMA;

    tm->downbeat_ms = bt_json_number(bt_json_get(jt, "downbeat_ms"), 0.0);

    /* A tempo map is accepted from day one so the song with a tempo change in
     * the bridge never forces a file-format migration; the UI exposes a single
     * BPM until it needs not to. */
    const bt_json *map = bt_json_get(jt, "map");
    if (map && bt_json_typeof(map) == BT_JSON_ARRAY && bt_json_len(map) > 0) {
        size_t n = bt_json_len(map);
        if (n > BT_MAX_TEMPO_SEG) return BT_ERR_SCHEMA;
        for (size_t i = 0; i < n; i++) {
            const bt_json *e = bt_json_at(map, i);
            double bpm  = bt_json_number(bt_json_get(e, "bpm"), 0.0);
            double beat = bt_json_number(bt_json_get(e, "beat"), 0.0);
            if (bpm <= 1.0 || bpm > 400.0) return BT_ERR_SCHEMA;
            tm->seg[i].bpm        = bpm;
            tm->seg[i].start_beat = (int64_t)beat;
            if (i > 0 && tm->seg[i].start_beat <= tm->seg[i - 1].start_beat)
                return BT_ERR_SCHEMA;          /* must be strictly increasing */
        }
        if (tm->seg[0].start_beat != 0) return BT_ERR_SCHEMA;
        tm->nseg = (int32_t)n;
        return BT_OK;
    }

    double bpm = bt_json_number(bt_json_get(jt, "bpm"), 0.0);
    if (bpm <= 1.0 || bpm > 400.0) return BT_ERR_SCHEMA;
    tm->seg[0].bpm        = bpm;
    tm->seg[0].start_beat = 0;
    tm->nseg              = 1;
    return BT_OK;
}

/* ----------------------------------------------------------------- tracks */

static bt_err bind_track(const bt_json *jt, bt_track *t) {
    memset(t, 0, sizeof(*t));

    const char *type = bt_json_string(bt_json_get(jt, "type"), "audio");
    if (strcmp(type, "click") == 0)      t->type = BT_TRACK_CLICK;
    else if (strcmp(type, "audio") == 0) t->type = BT_TRACK_AUDIO;
    else return BT_ERR_SCHEMA;

    copy_str(t->name, sizeof(t->name), bt_json_get(jt, "name"),
             t->type == BT_TRACK_CLICK ? "Click" : "Track");
    copy_str(t->bus,  sizeof(t->bus),  bt_json_get(jt, "bus"), "foh");
    copy_str(t->file, sizeof(t->file), bt_json_get(jt, "file"), "");

    if (!t->bus[0]) return BT_ERR_SCHEMA;
    if (t->type == BT_TRACK_AUDIO && !t->file[0]) return BT_ERR_SCHEMA;

    /* Reject absolute paths and traversal: a set list folder is meant to be a
     * self-contained unit you can copy to the backup laptop, and this is also
     * the file that a stranger's shared set list would arrive in. */
    if (t->file[0] == '/' || t->file[0] == '\\') return BT_ERR_SCHEMA;
    if (t->file[0] && t->file[1] == ':')         return BT_ERR_SCHEMA;
    if (strstr(t->file, "..") != NULL)           return BT_ERR_SCHEMA;

    t->gain_db   = bt_json_number(bt_json_get(jt, "gain_db"), 0.0);
    t->offset_ms = (int32_t)bt_json_number(bt_json_get(jt, "offset_ms"), 0.0);
    t->muted     = bt_json_bool(bt_json_get(jt, "muted"), false);

    if (t->gain_db < -96.0 || t->gain_db > 12.0)     return BT_ERR_SCHEMA;
    if (t->offset_ms < -60000 || t->offset_ms > 60000) return BT_ERR_SCHEMA;
    return BT_OK;
}

static bt_err bind_song(const bt_json *js, bt_song *s) {
    memset(s, 0, sizeof(*s));

    copy_str(s->title,  sizeof(s->title),  bt_json_get(js, "title"),  "Untitled");
    copy_str(s->artist, sizeof(s->artist), bt_json_get(js, "artist"), "");

    bt_err e = bind_tempo(bt_json_get(js, "tempo"), &s->tempo);
    if (e != BT_OK) return e;

    s->count_in_bars = (int32_t)bt_json_number(bt_json_get(js, "count_in_bars"), 1);
    if (s->count_in_bars < 0 || s->count_in_bars > 8) return BT_ERR_SCHEMA;

    const char *oe = bt_json_string(bt_json_get(js, "on_end"), "stop");
    if (strcmp(oe, "stop") == 0)      s->on_end = BT_ON_END_STOP;
    else if (strcmp(oe, "next") == 0) s->on_end = BT_ON_END_NEXT;
    else return BT_ERR_SCHEMA;

    const bt_json *tracks = bt_json_get(js, "tracks");
    if (!tracks || bt_json_typeof(tracks) != BT_JSON_ARRAY) return BT_ERR_SCHEMA;
    size_t n = bt_json_len(tracks);
    if (n == 0 || n > BT_MAX_TRACKS) return BT_ERR_SCHEMA;

    for (size_t i = 0; i < n; i++) {
        e = bind_track(bt_json_at(tracks, i), &s->track[i]);
        if (e != BT_OK) return e;
    }
    s->ntracks = (int32_t)n;
    return BT_OK;
}

/* ---------------------------------------------------------------- set list */

bt_err bt_setlist_load_mem(const char *text, size_t len, const char *dir,
                           bt_setlist **out, int *err_line) {
    if (!text || !out) return BT_ERR_RANGE;
    *out = NULL;

    bt_json *root = NULL;
    bt_err e = bt_json_parse(text, len, &root, err_line);
    if (e != BT_OK) return e;

    if (bt_json_typeof(root) != BT_JSON_OBJECT) { bt_json_free(root); return BT_ERR_SCHEMA; }

    int ver = (int)bt_json_number(bt_json_get(root, "version"), 0);
    if (ver != BT_SETLIST_VERSION) { bt_json_free(root); return BT_ERR_SCHEMA; }

    const bt_json *songs = bt_json_get(root, "songs");
    if (!songs || bt_json_typeof(songs) != BT_JSON_ARRAY) {
        bt_json_free(root);
        return BT_ERR_SCHEMA;
    }
    size_t n = bt_json_len(songs);

    bt_setlist *sl = (bt_setlist *)calloc(1, sizeof(*sl));
    if (!sl) { bt_json_free(root); return BT_ERR_ALLOC; }

    copy_str(sl->name, sizeof(sl->name), bt_json_get(root, "name"), "Set List");
    snprintf(sl->dir, sizeof(sl->dir), "%s", dir ? dir : "");

    if (n > 0) {
        sl->song = (bt_song *)calloc(n, sizeof(bt_song));
        if (!sl->song) { free(sl); bt_json_free(root); return BT_ERR_ALLOC; }
    }
    sl->cap = (int32_t)n;

    for (size_t i = 0; i < n; i++) {
        e = bind_song(bt_json_at(songs, i), &sl->song[i]);
        if (e != BT_OK) { bt_setlist_free(sl); bt_json_free(root); return e; }
        sl->nsongs++;
    }

    bt_json_free(root);
    *out = sl;
    return BT_OK;
}

bt_err bt_setlist_load_file(const char *path, bt_setlist **out, int *err_line) {
    if (!path || !out) return BT_ERR_RANGE;

    char  *buf = NULL;
    size_t len = 0;
    bt_err e = read_file(path, &buf, &len);
    if (e != BT_OK) return e;

    /* Paths inside the file resolve against the file's own directory. */
    char dir[BT_MAX_PATH];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
#ifdef _WIN32
    char *bslash = strrchr(dir, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
    if (slash) *slash = '\0'; else dir[0] = '\0';

    e = bt_setlist_load_mem(buf, len, dir, out, err_line);
    free(buf);
    return e;
}

/* ------------------------------------------------------------ device.json */

bt_err bt_device_cfg_load_mem(const char *text, size_t len, bt_device_cfg *out,
                              int *err_line) {
    if (!text || !out) return BT_ERR_RANGE;
    bt_device_cfg_defaults(out);

    bt_json *root = NULL;
    bt_err e = bt_json_parse(text, len, &root, err_line);
    if (e != BT_OK) return e;
    if (bt_json_typeof(root) != BT_JSON_OBJECT) { bt_json_free(root); return BT_ERR_SCHEMA; }

    copy_str(out->device, sizeof(out->device), bt_json_get(root, "device"), "default");
    copy_str(out->api,    sizeof(out->api),    bt_json_get(root, "api"),    "");
    out->sample_rate   = (int32_t)bt_json_number(bt_json_get(root, "sample_rate"), 48000);
    out->buffer_frames = (int32_t)bt_json_number(bt_json_get(root, "buffer_frames"), 512);

    if (out->sample_rate < 8000 || out->sample_rate > 192000) {
        bt_json_free(root); return BT_ERR_SCHEMA;
    }
    if (out->buffer_frames < 16 || out->buffer_frames > 8192) {
        bt_json_free(root); return BT_ERR_SCHEMA;
    }

    const bt_json *buses = bt_json_get(root, "buses");
    if (buses && bt_json_typeof(buses) == BT_JSON_ARRAY) {
        size_t n = bt_json_len(buses);
        if (n == 0 || n > BT_MAX_BUSES) { bt_json_free(root); return BT_ERR_SCHEMA; }
        memset(out->bus, 0, sizeof(out->bus));
        out->nbuses = 0;
        for (size_t i = 0; i < n; i++) {
            const bt_json *b = bt_json_at(buses, i);
            bt_bus *dst = &out->bus[i];
            copy_str(dst->name, sizeof(dst->name), bt_json_get(b, "name"), "");
            if (!dst->name[0]) { bt_json_free(root); return BT_ERR_SCHEMA; }

            const bt_json *ch = bt_json_get(b, "channels");
            size_t nch = bt_json_len(ch);
            if (nch < 1 || nch > BT_MAX_BUS_CH) { bt_json_free(root); return BT_ERR_SCHEMA; }
            for (size_t k = 0; k < nch; k++) {
                int32_t c = (int32_t)bt_json_number(bt_json_at(ch, k), -1);
                if (c < 0 || c >= BT_MAX_OUT_CH) { bt_json_free(root); return BT_ERR_SCHEMA; }
                dst->ch[k] = c;
            }
            dst->nch = (int32_t)nch;
            out->nbuses++;
        }
    }

    bt_json_free(root);
    return BT_OK;
}

bt_err bt_device_cfg_load_file(const char *path, bt_device_cfg *out, int *err_line) {
    if (!path || !out) return BT_ERR_RANGE;
    char  *buf = NULL;
    size_t len = 0;
    bt_err e = read_file(path, &buf, &len);
    if (e != BT_OK) return e;
    e = bt_device_cfg_load_mem(buf, len, out, err_line);
    free(buf);
    return e;
}
