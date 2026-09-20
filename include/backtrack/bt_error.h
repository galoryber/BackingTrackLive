/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#ifndef BT_ERROR_H
#define BT_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BT_OK = 0,
    BT_ERR_ALLOC,        /* out of memory                                  */
    BT_ERR_IO,           /* file could not be read                         */
    BT_ERR_PARSE,        /* malformed JSON                                 */
    BT_ERR_SCHEMA,       /* valid JSON, invalid set list                   */
    BT_ERR_FORMAT,       /* unsupported/corrupt audio file                 */
    BT_ERR_RATE,         /* sample-rate mismatch (see docs: Phase 2)       */
    BT_ERR_RANGE,        /* value out of accepted range                    */
    BT_ERR_NOT_FOUND,    /* named bus/song/track does not exist            */
    BT_ERR_STATE         /* call made in an invalid state                  */
} bt_err;

/* Human-readable, static storage, never NULL. */
const char *bt_strerror(bt_err e);

#ifdef __cplusplus
}
#endif
#endif /* BT_ERROR_H */
