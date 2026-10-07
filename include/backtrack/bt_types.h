/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#ifndef BT_TYPES_H
#define BT_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Frame position. SIGNED and 64-bit on purpose:
 *   - negative values are the count-in (click runs, audio is silent)
 *   - 64-bit removes any doubt about a long set at 96 kHz
 */
typedef int64_t bt_frame;

#define BT_MAX_NAME       64
/* A tuning note is read at a glance on a dark stage: "E1", "D1".
 * Short on purpose - anything long enough to be a sentence belongs
 * somewhere other than the middle of a performance. */
#define BT_MAX_TUNING     16
/* A cue line, read on stage mid-song: "drums in at 24", "vocals B34".
 * Room for a sentence, not for a paragraph - anything longer is not something
 * anyone reads while playing. */
#define BT_MAX_CUE       120
/* Lighting cues per song. Thirty-six songs of eight cues is the band's whole
 * show; this leaves room without pretending a song has a hundred moments. */
#define BT_MAX_LIGHT_CUES 64
#define BT_MAX_PATH      512
#define BT_MAX_BUS_CH      2     /* a bus is mono or stereo                */
#define BT_MAX_BUSES      16
#define BT_MAX_OUT_CH     64     /* physical device output channels        */
#define BT_MAX_TRACKS     32     /* per song                               */
#define BT_MAX_TEMPO_SEG  64     /* tempo changes within one song          */

#ifdef __cplusplus
}
#endif
#endif /* BT_TYPES_H */
