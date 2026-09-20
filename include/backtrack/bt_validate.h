/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * Checking a set list before a gig rather than during one.
 *
 * btrender and btplay both stop at the first problem, one song at a time.
 * Building a real 40-song set means finding out everything that is wrong in
 * one pass: which stems are missing, which bus names this machine cannot
 * route, how much RAM the thing will want, and the quieter mistakes - a stem
 * that is entirely silent because the wrong file was downloaded, a song with
 * no click, a nudge longer than the stem it nudges.
 *
 * Errors are things that will fail. Warnings are things that are probably
 * not what anyone meant. The distinction matters: a set list full of
 * warnings still plays.
 */
#ifndef BT_VALIDATE_H
#define BT_VALIDATE_H

#include "bt_types.h"
#include "bt_error.h"
#include "bt_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BT_ISSUE_ERROR = 0,   /* this set list will not play as written */
    BT_ISSUE_WARN         /* it will play, but probably not as intended */
} bt_issue_level;

typedef struct {
    bt_issue_level level;
    int32_t        song;    /* -1 when the issue is about the set as a whole */
    int32_t        track;   /* -1 when the issue is about the whole song     */
    char           msg[224];
} bt_issue;

typedef struct {
    int32_t  songs;
    int32_t  tracks;
    double   total_seconds;     /* audio, excluding count-ins */
    double   longest_seconds;
    size_t   peak_resident_bytes;   /* the two largest adjacent songs */
    size_t   all_resident_bytes;    /* if every song were held at once */
    int32_t  errors;
    int32_t  warnings;
} bt_setlist_stats;

/* Decodes every stem once, gathering issues and statistics, and frees as it
 * goes so memory stays bounded however long the set is.
 *
 * `dev` may be NULL, in which case bus names are not checked against anything.
 * `*issues` is allocated and must be freed by the caller. */
bt_err bt_setlist_validate(bt_setlist *sl, const bt_device_cfg *dev,
                           int32_t sample_rate,
                           bt_issue **issues, size_t *nissues,
                           bt_setlist_stats *stats);

const char *bt_issue_level_name(bt_issue_level l);

#ifdef __cplusplus
}
#endif
#endif /* BT_VALIDATE_H */
