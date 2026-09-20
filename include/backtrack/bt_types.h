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
