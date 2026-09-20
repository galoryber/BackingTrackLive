/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * RIFF/WAVE decode to planar float32, plus a 24-bit PCM writer.
 *
 * Planar (not interleaved) because the mixer walks one stem into one bus at a
 * time; interleaving would make every inner loop stride.
 */
#include "backtrack/bt_wav.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define WAVE_FORMAT_PCM        0x0001
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#define WAVE_FORMAT_EXTENSIBLE 0xFFFE

typedef struct {
    const unsigned char *p;
    size_t               len;
    size_t               pos;
} rd;

static bool rd_have(const rd *r, size_t n) { return r->pos + n <= r->len; }

static uint32_t rd_u32(rd *r) {
    uint32_t v = (uint32_t)r->p[r->pos]
               | ((uint32_t)r->p[r->pos + 1] << 8)
               | ((uint32_t)r->p[r->pos + 2] << 16)
               | ((uint32_t)r->p[r->pos + 3] << 24);
    r->pos += 4;
    return v;
}

static uint16_t rd_u16(rd *r) {
    uint16_t v = (uint16_t)(r->p[r->pos] | ((uint16_t)r->p[r->pos + 1] << 8));
    r->pos += 2;
    return v;
}

/* Reads one sample of `bits` at byte offset `o`, normalised to [-1, 1].
 * Caller has already bounds-checked the frame block. */
static float sample_at(const unsigned char *b, size_t o, int bits, bool is_float) {
    if (is_float) {
        if (bits == 32) {
            uint32_t u = (uint32_t)b[o] | ((uint32_t)b[o+1] << 8)
                       | ((uint32_t)b[o+2] << 16) | ((uint32_t)b[o+3] << 24);
            float f; memcpy(&f, &u, 4);
            return f;
        }
        uint64_t u = 0;
        for (int i = 7; i >= 0; i--) u = (u << 8) | b[o + (size_t)i];
        double d; memcpy(&d, &u, 8);
        return (float)d;
    }
    switch (bits) {
    case 8:  /* 8-bit WAV is unsigned */
        return ((float)b[o] - 128.0f) / 128.0f;
    case 16: {
        int16_t v = (int16_t)((uint16_t)b[o] | ((uint16_t)b[o+1] << 8));
        return (float)v / 32768.0f;
    }
    case 24: {
        int32_t v = (int32_t)(((uint32_t)b[o] << 8) | ((uint32_t)b[o+1] << 16)
                            | ((uint32_t)b[o+2] << 24));
        return (float)(v >> 8) / 8388608.0f;
    }
    case 32: {
        int32_t v = (int32_t)((uint32_t)b[o] | ((uint32_t)b[o+1] << 8)
                            | ((uint32_t)b[o+2] << 16) | ((uint32_t)b[o+3] << 24));
        return (float)v / 2147483648.0f;
    }
    default:
        return 0.0f;
    }
}

bt_err bt_wav_decode(const void *data, size_t len, bt_wav *out) {
    if (!data || !out) return BT_ERR_RANGE;
    memset(out, 0, sizeof(*out));

    rd r = { (const unsigned char *)data, len, 0 };

    if (!rd_have(&r, 12)) return BT_ERR_FORMAT;
    if (memcmp(r.p, "RIFF", 4) != 0 || memcmp(r.p + 8, "WAVE", 4) != 0)
        return BT_ERR_FORMAT;
    r.pos = 12;

    int  channels = 0, bits = 0, rate = 0;
    bool is_float = false, have_fmt = false;
    const unsigned char *pcm = NULL;
    size_t pcm_len = 0;

    while (rd_have(&r, 8)) {
        char id[4];
        memcpy(id, r.p + r.pos, 4);
        r.pos += 4;
        uint32_t sz = rd_u32(&r);

        /* A truncated final chunk is common in the wild (and in fuzz corpora);
         * clamp rather than reject so we still decode what is there. */
        size_t avail = r.len - r.pos;
        size_t csz   = (sz > avail) ? avail : (size_t)sz;

        if (memcmp(id, "fmt ", 4) == 0) {
            if (csz < 16) return BT_ERR_FORMAT;
            size_t base = r.pos;
            uint16_t fmt = rd_u16(&r);
            channels     = (int)rd_u16(&r);
            rate         = (int)rd_u32(&r);
            r.pos += 4;                    /* byte rate  */
            r.pos += 2;                    /* block align */
            bits         = (int)rd_u16(&r);

            if (fmt == WAVE_FORMAT_EXTENSIBLE) {
                if (csz < 40) return BT_ERR_FORMAT;
                /* cbSize(2) validBits(2) channelMask(4) then the GUID, whose
                 * first two bytes are the real format tag. */
                size_t g = base + 24;
                if (g + 2 > r.len) return BT_ERR_FORMAT;
                fmt = (uint16_t)(r.p[g] | ((uint16_t)r.p[g + 1] << 8));
            }
            if (fmt == WAVE_FORMAT_IEEE_FLOAT) is_float = true;
            else if (fmt != WAVE_FORMAT_PCM)   return BT_ERR_FORMAT;

            have_fmt = true;
            r.pos = base + csz;
        } else if (memcmp(id, "data", 4) == 0) {
            pcm     = r.p + r.pos;
            pcm_len = csz;
            r.pos  += csz;
        } else {
            r.pos += csz;
        }
        if (csz & 1) r.pos++;              /* chunks are word-aligned */
        if (r.pos > r.len) break;
    }

    if (!have_fmt || !pcm) return BT_ERR_FORMAT;
    if (channels <= 0 || channels > BT_MAX_OUT_CH) return BT_ERR_FORMAT;
    if (rate <= 0) return BT_ERR_FORMAT;
    if (is_float) { if (bits != 32 && bits != 64) return BT_ERR_FORMAT; }
    else if (bits != 8 && bits != 16 && bits != 24 && bits != 32) return BT_ERR_FORMAT;

    size_t bps        = (size_t)bits / 8;
    size_t frame_size = bps * (size_t)channels;
    if (frame_size == 0) return BT_ERR_FORMAT;
    bt_frame frames = (bt_frame)(pcm_len / frame_size);
    if (frames <= 0) return BT_ERR_FORMAT;

    float **plan = (float **)calloc((size_t)channels, sizeof(*plan));
    if (!plan) return BT_ERR_ALLOC;
    for (int c = 0; c < channels; c++) {
        plan[c] = (float *)malloc((size_t)frames * sizeof(float));
        if (!plan[c]) {
            for (int k = 0; k < c; k++) free(plan[k]);
            free(plan);
            return BT_ERR_ALLOC;
        }
    }

    for (bt_frame f = 0; f < frames; f++) {
        size_t base = (size_t)f * frame_size;
        for (int c = 0; c < channels; c++)
            plan[c][f] = sample_at(pcm, base + (size_t)c * bps, bits, is_float);
    }

    out->pcm         = plan;
    out->channels    = channels;
    out->sample_rate = rate;
    out->frames      = frames;
    return BT_OK;
}

bt_err bt_wav_read_file(const char *path, bt_wav *out) {
    if (!path || !out) return BT_ERR_RANGE;
    FILE *f = fopen(path, "rb");
    if (!f) return BT_ERR_IO;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return BT_ERR_IO; }
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return BT_ERR_IO; }
    rewind(f);

    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) { fclose(f); return BT_ERR_ALLOC; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return BT_ERR_IO; }

    bt_err e = bt_wav_decode(buf, got, out);
    free(buf);
    return e;
}

void bt_wav_free(bt_wav *w) {
    if (!w || !w->pcm) return;
    for (int c = 0; c < w->channels; c++) free(w->pcm[c]);
    free(w->pcm);
    memset(w, 0, sizeof(*w));
}

/* ------------------------------------------------------------------ writer */

static void put_u32(FILE *f, uint32_t v) {
    unsigned char b[4] = { (unsigned char)(v), (unsigned char)(v >> 8),
                           (unsigned char)(v >> 16), (unsigned char)(v >> 24) };
    fwrite(b, 1, 4, f);
}

static void put_u16(FILE *f, uint16_t v) {
    unsigned char b[2] = { (unsigned char)(v), (unsigned char)(v >> 8) };
    fwrite(b, 1, 2, f);
}

bt_err bt_wav_write_file(const char *path, const float *const *pcm,
                         int32_t channels, int32_t sample_rate, bt_frame frames) {
    if (!path || !pcm || channels <= 0 || frames < 0) return BT_ERR_RANGE;

    FILE *f = fopen(path, "wb");
    if (!f) return BT_ERR_IO;

    const uint32_t bps       = 3;
    const uint32_t block     = bps * (uint32_t)channels;
    const uint32_t data_size = (uint32_t)frames * block;

    fwrite("RIFF", 1, 4, f);
    put_u32(f, 36 + data_size);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    put_u32(f, 16);
    put_u16(f, WAVE_FORMAT_PCM);
    put_u16(f, (uint16_t)channels);
    put_u32(f, (uint32_t)sample_rate);
    put_u32(f, (uint32_t)sample_rate * block);
    put_u16(f, (uint16_t)block);
    put_u16(f, (uint16_t)(bps * 8));

    fwrite("data", 1, 4, f);
    put_u32(f, data_size);

    for (bt_frame i = 0; i < frames; i++) {
        for (int32_t c = 0; c < channels; c++) {
            float s = pcm[c][i];
            if (s >  1.0f) s =  1.0f;
            if (s < -1.0f) s = -1.0f;
            int32_t v = (int32_t)lrintf(s * 8388607.0f);
            unsigned char b[3] = { (unsigned char)(v), (unsigned char)(v >> 8),
                                   (unsigned char)(v >> 16) };
            fwrite(b, 1, 3, f);
        }
    }

    bool ok = (ferror(f) == 0);
    if (fclose(f) != 0) ok = false;
    return ok ? BT_OK : BT_ERR_IO;
}
