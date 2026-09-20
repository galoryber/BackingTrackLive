/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#include "backtrack/bt_wav.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    bt_wav w;
    if (bt_wav_decode(data, size, &w) == BT_OK) bt_wav_free(&w);
    return 0;
}
