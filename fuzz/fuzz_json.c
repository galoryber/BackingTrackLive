/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#include "backtrack/bt_json.h"
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char *buf = (char *)malloc(size + 1);
    if (!buf) return 0;
    memcpy(buf, data, size);
    buf[size] = '\0';

    bt_json *j = NULL;
    int line = 0;
    if (bt_json_parse(buf, size, &j, &line) == BT_OK) {
        /* Walk what we got, so the fuzzer also exercises the accessors. */
        (void)bt_json_len(j);
        const bt_json *k = bt_json_get(j, "version");
        (void)bt_json_number(k, 0.0);
        (void)bt_json_string(bt_json_at(j, 0), "");
        bt_json_free(j);
    }
    free(buf);
    return 0;
}
