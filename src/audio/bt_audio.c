/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Format detection and decode dispatch.
 */
#include "backtrack/bt_audio.h"
#include "backtrack/bt_wav.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define DR_MP3_NO_STDIO
#include "dr_mp3.h"
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#include "dr_flac.h"

/* A stem longer than this is not a stem. Bounds what a malformed or hostile
 * header can convince a decoder to allocate: 30 minutes at 192 kHz. */
#define BT_MAX_DECODE_FRAMES ((bt_frame)192000 * 60 * 30)

const char *bt_audio_formats(void) { return "WAV, FLAC, MP3"; }

void bt_audio_free(bt_audio *a) {
    if (!a || !a->pcm) return;
    for (int32_t c = 0; c < a->channels; c++) free(a->pcm[c]);
    free(a->pcm);
    memset(a, 0, sizeof(*a));
}

/* Splits an interleaved float block into the planar layout everything
 * downstream expects, and takes ownership of freeing the source. */
static bt_err to_planar(float *inter, uint64_t frames, uint32_t channels,
                        uint32_t rate, bt_audio *out) {
    memset(out, 0, sizeof(*out));
    if (!inter) return BT_ERR_FORMAT;

    if (channels == 0 || channels > BT_MAX_OUT_CH
        || frames == 0 || frames > (uint64_t)BT_MAX_DECODE_FRAMES
        || rate == 0) {
        free(inter);
        return BT_ERR_FORMAT;
    }

    float **plan = (float **)calloc(channels, sizeof(*plan));
    if (!plan) { free(inter); return BT_ERR_ALLOC; }

    for (uint32_t c = 0; c < channels; c++) {
        plan[c] = (float *)malloc((size_t)frames * sizeof(float));
        if (!plan[c]) {
            for (uint32_t k = 0; k < c; k++) free(plan[k]);
            free(plan);
            free(inter);
            return BT_ERR_ALLOC;
        }
    }
    for (uint64_t i = 0; i < frames; i++)
        for (uint32_t c = 0; c < channels; c++)
            plan[c][i] = inter[i * channels + c];

    free(inter);
    out->pcm         = plan;
    out->channels    = (int32_t)channels;
    out->sample_rate = (int32_t)rate;
    out->frames      = (bt_frame)frames;
    return BT_OK;
}

static bt_err decode_mp3(const void *data, size_t len, bt_audio *out) {
    drmp3_config cfg;
    drmp3_uint64 frames = 0;
    memset(&cfg, 0, sizeof(cfg));
    float *inter = drmp3_open_memory_and_read_pcm_frames_f32(data, len, &cfg,
                                                             &frames, NULL);
    if (!inter) return BT_ERR_FORMAT;
    return to_planar(inter, frames, cfg.channels, cfg.sampleRate, out);
}

static bt_err decode_flac(const void *data, size_t len, bt_audio *out) {
    unsigned int  channels = 0, rate = 0;
    drflac_uint64 frames   = 0;
    float *inter = drflac_open_memory_and_read_pcm_frames_f32(data, len,
                                                              &channels, &rate,
                                                              &frames, NULL);
    if (!inter) return BT_ERR_FORMAT;
    return to_planar(inter, frames, channels, rate, out);
}

static bool ext_is(const char *ext, const char *want) {
    if (!ext) return false;
    return
#ifdef _WIN32
        _stricmp(ext, want) == 0;
#else
        strcasecmp(ext, want) == 0;
#endif
}

bt_err bt_audio_decode_mem(const void *data, size_t len, const char *hint_ext,
                           bt_audio *out) {
    if (!data || !out) return BT_ERR_RANGE;
    memset(out, 0, sizeof(*out));
    if (len < 4) return BT_ERR_FORMAT;

    const unsigned char *b = (const unsigned char *)data;

    /* Content first: a stem named .wav that is really an MP3 happens whenever
     * somebody re-saves a file, and there is no reason for that to fail. */
    if (len >= 12 && memcmp(b, "RIFF", 4) == 0 && memcmp(b + 8, "WAVE", 4) == 0)
        return bt_wav_decode(data, len, out);

    if (memcmp(b, "fLaC", 4) == 0)
        return decode_flac(data, len, out);

    /* MP3 has no dependable magic: an ID3 tag or a frame sync is the best
     * available signal, and the extension is the tiebreak. */
    if (memcmp(b, "ID3", 3) == 0)
        return decode_mp3(data, len, out);
    if (b[0] == 0xFF && (b[1] & 0xE0) == 0xE0)
        return decode_mp3(data, len, out);

    if (ext_is(hint_ext, "mp3"))  return decode_mp3(data, len, out);
    if (ext_is(hint_ext, "flac")) return decode_flac(data, len, out);
    if (ext_is(hint_ext, "wav"))  return bt_wav_decode(data, len, out);

    return BT_ERR_FORMAT;
}

bt_err bt_audio_decode_file(const char *path, bt_audio *out) {
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

    const char *dot = strrchr(path, '.');
    bt_err e = bt_audio_decode_mem(buf, got, dot ? dot + 1 : NULL, out);
    free(buf);
    return e;
}
