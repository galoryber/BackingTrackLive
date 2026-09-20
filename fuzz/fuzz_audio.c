/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * The whole decode dispatch, including the vendored MP3 and FLAC decoders.
 * Stems are files from the internet; this is where hostile input arrives. */
#include "backtrack/bt_audio.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    bt_audio a;
    if (bt_audio_decode_mem(data, size, NULL, &a) == BT_OK) bt_audio_free(&a);
    return 0;
}
