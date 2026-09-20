/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#include "backtrack/bt_error.h"

const char *bt_strerror(bt_err e) {
    switch (e) {
    case BT_OK:            return "ok";
    case BT_ERR_ALLOC:     return "out of memory";
    case BT_ERR_IO:        return "file could not be read";
    case BT_ERR_PARSE:     return "malformed JSON";
    case BT_ERR_SCHEMA:    return "invalid set list";
    case BT_ERR_FORMAT:    return "unsupported or corrupt audio file";
    case BT_ERR_RATE:      return "sample rate mismatch";
    case BT_ERR_RANGE:     return "value out of range";
    case BT_ERR_NOT_FOUND: return "not found";
    case BT_ERR_STATE:     return "invalid state";
    }
    return "unknown error";
}
