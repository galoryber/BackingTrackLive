/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Writes a complete, runnable set list: stems, setlist.json and device.json.
 *
 * In the library rather than in a tool because both the CLI and the UI offer
 * it, and two copies of "what a starter set looks like" would drift.
 */
#ifndef BT_DEMO_H
#define BT_DEMO_H

#include "bt_types.h"
#include "bt_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* `seconds` is the length of each stem. Two minutes by default elsewhere:
 * short stems are fine for checking routing and useless for measuring
 * dropouts, since a machine that drops a buffer three times in ten minutes
 * reads zero over ten seconds.
 *
 * `progress` may be NULL; it is called with each file as it is written, which
 * matters in a UI because two minutes of stems takes a noticeable moment. */
typedef void (*bt_demo_progress)(const char *what, void *user);

bt_err bt_demo_write(const char *dir, double seconds,
                     bt_demo_progress progress, void *user);

#ifdef __cplusplus
}
#endif
#endif /* BT_DEMO_H */
