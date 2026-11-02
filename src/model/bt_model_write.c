/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Serialising the model back to JSON.
 *
 * Two properties are deliberate and both are tested:
 *
 *   Lossless   - load -> save -> load returns the identical model. Numbers are
 *                emitted at the shortest precision that parses back exactly,
 *                so a 156.37 BPM does not become 156.36999999999999.
 *   Stable     - saving the same model twice produces byte-identical output.
 *                A set list is a text file people keep in git and edit by
 *                hand; churn in a diff that nobody asked for is a defect.
 */
#include "backtrack/bt_model.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "bt_write_util.h"


static void sb_reserve(sb *s, size_t extra) {
    if (s->failed) return;
    if (s->len + extra + 1 <= s->cap) return;
    size_t cap = s->cap ? s->cap : 1024;
    while (cap < s->len + extra + 1) cap *= 2;
    char *q = (char *)realloc(s->buf, cap);
    if (!q) { s->failed = true; return; }
    s->buf = q;
    s->cap = cap;
}

void bt__sb_raw(sb *s, const char *text, size_t n) {
    sb_reserve(s, n);
    if (s->failed) return;
    memcpy(s->buf + s->len, text, n);
    s->len += n;
    s->buf[s->len] = '\0';
}

void bt__sb_str(sb *s, const char *text) { bt__sb_raw(s, text, strlen(text)); }

void bt__sb_fmt(sb *s, const char *fmt, ...) {
#ifdef _MSC_VER
    __pragma(warning(push))
#endif
    va_list ap;
    va_start(ap, fmt);
    char tmp[512];
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) { s->failed = true; return; }
    if ((size_t)n >= sizeof(tmp)) { s->failed = true; return; }
    bt__sb_raw(s, tmp, (size_t)n);
#ifdef _MSC_VER
    __pragma(warning(pop))
#endif
}

/* JSON string escaping. Control characters must be escaped or the file we
 * write is not parseable by the parser we wrote. */
void bt__sb_json_string(sb *s, const char *v) {
    bt__sb_str(s, "\"");
    for (const unsigned char *p = (const unsigned char *)v; *p; p++) {
        switch (*p) {
        case '"':  bt__sb_str(s, "\\\""); break;
        case '\\': bt__sb_str(s, "\\\\"); break;
        case '\b': bt__sb_str(s, "\\b");  break;
        case '\f': bt__sb_str(s, "\\f");  break;
        case '\n': bt__sb_str(s, "\\n");  break;
        case '\r': bt__sb_str(s, "\\r");  break;
        case '\t': bt__sb_str(s, "\\t");  break;
        default:
            if (*p < 0x20) bt__sb_fmt(s, "\\u%04x", (unsigned)*p);
            else           bt__sb_raw(s, (const char *)p, 1);
            break;
        }
    }
    bt__sb_str(s, "\"");
}

/* Shortest representation that parses back to the identical double. %.17g
 * always round-trips but is ugly; try progressively shorter forms and keep the
 * first that survives a parse. */
static void sb_number(sb *s, double v) {
    char tmp[64];
    bt_format_number(v, tmp, sizeof(tmp));
    /* JSON has no Infinity or NaN. Nothing in the model should ever be either;
     * if one appears, write a zero rather than an unparseable file. */
    if (strstr(tmp, "inf") || strstr(tmp, "nan") || strstr(tmp, "INF")
        || strstr(tmp, "NAN")) {
        bt__sb_str(s, "0");
        return;
    }
    bt__sb_str(s, tmp);
}

/* ------------------------------------------------------------- set list */

static void write_tempo(sb *s, const bt_tempo_map *tm) {
    bt__sb_str(s, "\"tempo\": { ");
    if (tm->nseg > 1) {
        bt__sb_str(s, "\"map\": [");
        for (int32_t i = 0; i < tm->nseg; i++) {
            bt__sb_str(s, i ? ", " : " ");
            bt__sb_str(s, "{ \"beat\": ");
            bt__sb_fmt(s, "%lld", (long long)tm->seg[i].start_beat);
            bt__sb_str(s, ", \"bpm\": ");
            sb_number(s, tm->seg[i].bpm);
            bt__sb_str(s, " }");
        }
        bt__sb_str(s, " ]");
    } else {
        bt__sb_str(s, "\"bpm\": ");
        sb_number(s, tm->nseg ? tm->seg[0].bpm : 120.0);
    }
    bt__sb_fmt(s, ", \"sig\": [%d, %d]", tm->sig_num, tm->sig_den);
    bt__sb_str(s, ", \"downbeat_ms\": ");
    sb_number(s, tm->downbeat_ms);
    bt__sb_str(s, " }");
}

static void write_track(sb *s, const bt_track *t) {
    bt__sb_str(s, "        { \"name\": ");
    bt__sb_json_string(s, t->name);
    bt__sb_str(s, ", \"type\": ");
    bt__sb_json_string(s, t->type == BT_TRACK_CLICK ? "click" : "audio");
    bt__sb_str(s, ", \"bus\": ");
    bt__sb_json_string(s, t->bus);
    if (t->type == BT_TRACK_AUDIO) {
        bt__sb_str(s, ",\n          \"file\": ");
        bt__sb_json_string(s, t->file);
    }
    bt__sb_str(s, ", \"gain_db\": ");
    sb_number(s, t->gain_db);
    bt__sb_fmt(s, ", \"offset_ms\": %d", t->offset_ms);
    bt__sb_fmt(s, ", \"muted\": %s", t->muted ? "true" : "false");
    bt__sb_str(s, " }");
}

static void write_song(sb *s, const bt_song *song) {
    bt__sb_str(s, "    {\n      \"title\": ");
    bt__sb_json_string(s, song->title);
    bt__sb_str(s, ", \"artist\": ");
    bt__sb_json_string(s, song->artist);
    bt__sb_str(s, ",\n      ");
    write_tempo(s, &song->tempo);
    /* Omitted when empty, so a set list written before this existed re-saves
     * byte-identically. */
    if (song->tuning[0]) {
        bt__sb_fmt(s, ",\n      \"tuning\": ");
        bt__sb_json_string(s, song->tuning);
    }
    if (song->cue[0]) {
        bt__sb_fmt(s, ",\n      \"cue\": ");
        bt__sb_json_string(s, song->cue);
    }
    if (song->midi_program >= 0)
        bt__sb_fmt(s, ",\n      \"midi_program\": %d", song->midi_program);
    bt__sb_fmt(s, ",\n      \"count_in_bars\": %d", song->count_in_bars);
    /* Omitted when zero: it is the normal case, and writing it would add a
     * line to every song in every set list written before this existed. */
    if (song->length_bars > 0)
        bt__sb_fmt(s, ", \"length_bars\": %d", song->length_bars);
    bt__sb_fmt(s, ", \"on_end\": ");
    bt__sb_json_string(s, song->on_end == BT_ON_END_NEXT ? "next" : "stop");
    bt__sb_str(s, ",\n      \"tracks\": [\n");
    for (int32_t i = 0; i < song->ntracks; i++) {
        write_track(s, &song->track[i]);
        bt__sb_str(s, i + 1 < song->ntracks ? ",\n" : "\n");
    }
    bt__sb_str(s, "      ]");

    /* Cues last, because they are the part somebody reads down a column of
     * when checking them against the lighting desk. Omitted entirely when a
     * song has none, which is every song in every set list written before
     * this existed. */
    if (song->nlight_cues > 0) {
        bt__sb_str(s, ",\n      \"light_cues\": [\n");
        for (int32_t i = 0; i < song->nlight_cues; i++) {
            const bt_light_event *c = &song->light_cue[i];
            /* Same field names the lighting side's cue file uses, so the two
             * read alike when somebody has both open. */
            bt__sb_fmt(s, "        { \"bar\": %d, \"ch\": %d, \"note\": %d",
                   c->bar, c->ch, c->note);
            if (c->kind != BT_LIGHT_LOOK)
                bt__sb_fmt(s, ", \"type\": \"%s\"",
                       c->kind == BT_LIGHT_HIT ? "hit" : "end");
            if (c->label[0]) {
                bt__sb_str(s, ", \"label\": ");
                bt__sb_json_string(s, c->label);
            }
            bt__sb_str(s, " }");
            bt__sb_str(s, i + 1 < song->nlight_cues ? ",\n" : "\n");
        }
        bt__sb_str(s, "      ]");
    }

    bt__sb_str(s, "\n    }");
}

bt_err bt_setlist_to_json(const bt_setlist *sl, char **out, size_t *len) {
    if (!sl || !out) return BT_ERR_RANGE;
    *out = NULL;
    if (len) *len = 0;

    sb s;
    memset(&s, 0, sizeof(s));

    bt__sb_str(&s, "{\n  \"version\": 1,\n  \"name\": ");
    bt__sb_json_string(&s, sl->name);


    bt__sb_str(&s, ",\n  \"songs\": [\n");
    for (int32_t i = 0; i < sl->nsongs; i++) {
        write_song(&s, &sl->song[i]);
        bt__sb_str(&s, i + 1 < sl->nsongs ? ",\n" : "\n");
    }
    bt__sb_str(&s, "  ]\n}\n");

    if (s.failed) { free(s.buf); return BT_ERR_ALLOC; }
    *out = s.buf;
    if (len) *len = s.len;
    return BT_OK;
}

/* --------------------------------------------------------- device.json */

bt_err bt_device_cfg_to_json(const bt_device_cfg *cfg, char **out, size_t *len) {
    if (!cfg || !out) return BT_ERR_RANGE;
    *out = NULL;
    if (len) *len = 0;

    sb s;
    memset(&s, 0, sizeof(s));

    bt__sb_str(&s, "{\n  \"device\": ");
    bt__sb_json_string(&s, cfg->device);
    bt__sb_str(&s, ",\n  \"api\": ");
    bt__sb_json_string(&s, cfg->api);
    /* Omitted when there is no lighting, so a device.json written before this
     * re-saves byte-identically. */
    if (cfg->midi_out[0]) {
        bt__sb_str(&s, ",\n  \"midi_out\": ");
        bt__sb_json_string(&s, cfg->midi_out);
    }
    bt__sb_fmt(&s, ",\n  \"sample_rate\": %d,\n  \"buffer_frames\": %d,\n",
           cfg->sample_rate, cfg->buffer_frames);
    bt__sb_str(&s, "  \"buses\": [\n");
    for (int32_t i = 0; i < cfg->nbuses; i++) {
        bt__sb_str(&s, "    { \"name\": ");
        bt__sb_json_string(&s, cfg->bus[i].name);
        bt__sb_str(&s, ", \"channels\": [");
        for (int32_t k = 0; k < cfg->bus[i].nch; k++)
            bt__sb_fmt(&s, "%s%d", k ? ", " : "", cfg->bus[i].ch[k]);
        bt__sb_str(&s, "] }");
        bt__sb_str(&s, i + 1 < cfg->nbuses ? ",\n" : "\n");
    }
    bt__sb_str(&s, "  ]\n}\n");

    if (s.failed) { free(s.buf); return BT_ERR_ALLOC; }
    *out = s.buf;
    if (len) *len = s.len;
    return BT_OK;
}

/* ------------------------------------------------------- atomic writing */

/* Writes to a temporary alongside the target and renames over it. An
 * interrupted save must not leave a half-written set list where a working one
 * used to be - the failure mode being avoided is losing a set an hour before
 * a gig. */
bt_err bt__write_atomic(const char *path, const char *data, size_t len) {
    char tmp[BT_MAX_PATH * 2];
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(tmp)) return BT_ERR_RANGE;

    FILE *f = fopen(tmp, "wb");
    if (!f) return BT_ERR_IO;
    size_t wrote = fwrite(data, 1, len, f);
    bool ok = (wrote == len) && (ferror(f) == 0);
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(tmp); return BT_ERR_IO; }

    /* rename() will not replace an existing file on Windows. */
    remove(path);
    if (rename(tmp, path) != 0) { remove(tmp); return BT_ERR_IO; }
    return BT_OK;
}

bt_err bt_setlist_save_file(const bt_setlist *sl, const char *path) {
    if (!sl || !path) return BT_ERR_RANGE;
    char  *json = NULL;
    size_t len  = 0;
    bt_err e = bt_setlist_to_json(sl, &json, &len);
    if (e != BT_OK) return e;
    e = bt__write_atomic(path, json, len);
    free(json);
    return e;
}

bt_err bt_device_cfg_save_file(const bt_device_cfg *cfg, const char *path) {
    if (!cfg || !path) return BT_ERR_RANGE;
    char  *json = NULL;
    size_t len  = 0;
    bt_err e = bt_device_cfg_to_json(cfg, &json, &len);
    if (e != BT_OK) return e;
    e = bt__write_atomic(path, json, len);
    free(json);
    return e;
}
