/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0.
 *
 * The whole schema-binding path, not just the tokenizer: a set list is the
 * file a stranger would one day hand you. */
#include "backtrack/bt_model.h"
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char *buf = (char *)malloc(size + 1);
    if (!buf) return 0;
    memcpy(buf, data, size);
    buf[size] = '\0';

    bt_setlist *sl = NULL;
    int line = 0;
    if (bt_setlist_load_mem(buf, size, "", &sl, &line) == BT_OK) bt_setlist_free(sl);

    bt_device_cfg cfg;
    if (bt_device_cfg_load_mem(buf, size, &cfg, &line) == BT_OK) { /* POD */ }

    free(buf);
    return 0;
}
