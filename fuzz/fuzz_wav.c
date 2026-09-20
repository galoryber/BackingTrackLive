/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#include "backtrack/bt_wav.h"
#include "backtrack/bt_audio.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    bt_audio w;
    if (bt_wav_decode(data, size, &w) == BT_OK) bt_audio_free(&w);
    return 0;
}
