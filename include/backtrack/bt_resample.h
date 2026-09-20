/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Load-time sample-rate conversion.
 *
 * This runs once per stem when a song is loaded, never in the audio callback,
 * so it is free to spend cycles on a long, obviously-correct filter rather
 * than a fast approximate one.
 *
 * Why in-tree rather than libsamplerate: the realtime constraints that make a
 * general resampler complicated do not apply here. Audio sample rates are
 * always rational ratios (44100/48000 reduces to 147/160), so the filter is a
 * precomputed polyphase bank with no transcendentals evaluated per sample, and
 * the whole thing is ~200 lines with no build-system entanglement on three
 * platforms. tests/test_resample.c measures the filter it actually produces -
 * passband flatness, stopband rejection and absence of aliasing - rather than
 * trusting that it looks right.
 */
#ifndef BT_RESAMPLE_H
#define BT_RESAMPLE_H

#include "bt_types.h"
#include "bt_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Converts planar float audio from `in_rate` to `out_rate`.
 *
 * Allocates `*out` as a fresh planar buffer of `channels` arrays, which the
 * caller frees (each channel, then the array). When the rates match, this is
 * a straight copy.
 *
 * Signal outside the input is treated as silence, so the first and last few
 * milliseconds are filter ramp rather than exact - correct for a finite
 * signal, and inaudible against stems that begin and end in silence anyway. */
bt_err bt_resample_planar(const float *const *in, int32_t channels,
                          bt_frame in_frames, int32_t in_rate, int32_t out_rate,
                          float ***out, bt_frame *out_frames);

/* Output length for a given input length and rate pair. Exposed so callers can
 * size buffers and report progress without resampling first. */
bt_frame bt_resample_out_frames(bt_frame in_frames, int32_t in_rate, int32_t out_rate);

#ifdef __cplusplus
}
#endif
#endif /* BT_RESAMPLE_H */
