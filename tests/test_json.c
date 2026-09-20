/* Copyright 2026 Gary Lobermier. Licensed under the Apache License, Version 2.0. */
#include "bt_test.h"
#include "backtrack/bt_json.h"

static bt_json *ok_parse(const char *s) {
    bt_json *j = NULL;
    int line = 0;
    bt_err e = bt_json_parse(s, strlen(s), &j, &line);
    BT_CHECK_EQI(e, BT_OK);
    return j;
}

static void reject(const char *s) {
    bt_json *j = NULL;
    int line = 0;
    bt_err e = bt_json_parse(s, strlen(s), &j, &line);
    bt_checks++;
    if (e == BT_OK) {
        bt_fails++;
        fprintf(stderr, "  FAIL accepted invalid JSON: %s\n", s);
        bt_json_free(j);
    }
    BT_CHECK(j == NULL);
}

static void test_scalars(void) {
    bt_json *j = ok_parse("{\"a\":1,\"b\":true,\"c\":\"x\",\"d\":null,\"e\":-2.5e3}");
    BT_CHECK_EQI(bt_json_typeof(j), BT_JSON_OBJECT);
    BT_CHECK_NEAR(bt_json_number(bt_json_get(j, "a"), 0), 1.0, 1e-12);
    BT_CHECK(bt_json_bool(bt_json_get(j, "b"), false) == true);
    BT_CHECK(strcmp(bt_json_string(bt_json_get(j, "c"), ""), "x") == 0);
    BT_CHECK_EQI(bt_json_typeof(bt_json_get(j, "d")), BT_JSON_NULL);
    BT_CHECK_NEAR(bt_json_number(bt_json_get(j, "e"), 0), -2500.0, 1e-9);
    /* Wrong-type access falls back to the supplied default. */
    BT_CHECK_NEAR(bt_json_number(bt_json_get(j, "c"), 42.0), 42.0, 1e-12);
    BT_CHECK(bt_json_get(j, "nope") == NULL);
    bt_json_free(j);
}

static void test_arrays(void) {
    bt_json *j = ok_parse("[1,[2,[3]],{\"k\":[]}]");
    BT_CHECK_EQI(bt_json_len(j), 3);
    BT_CHECK_NEAR(bt_json_number(bt_json_at(j, 0), 0), 1.0, 1e-12);
    BT_CHECK_EQI(bt_json_len(bt_json_at(j, 1)), 2);
    BT_CHECK(bt_json_at(j, 99) == NULL);
    bt_json_free(j);
}

static void test_strings(void) {
    bt_json *j = ok_parse("\"a\\nb\\t\\\"c\\\"\\u0041\\u00e9\\uD83D\\uDE00\"");
    const char *s = bt_json_string(j, "");
    BT_CHECK(strncmp(s, "a\nb\t\"c\"A", 8) == 0);
    /* U+00E9 is two UTF-8 bytes, U+1F600 is four. */
    BT_CHECK_EQI(strlen(s), 8 + 2 + 4);
    bt_json_free(j);

    bt_json *b = ok_parse("\"\"");
    BT_CHECK(strcmp(bt_json_string(b, "x"), "") == 0);
    bt_json_free(b);
}

static void test_bom(void) {
    /* Notepad on Windows will do this to a set list sooner or later. */
    bt_json *j = ok_parse("\xEF\xBB\xBF{\"v\":1}");
    BT_CHECK_NEAR(bt_json_number(bt_json_get(j, "v"), 0), 1.0, 1e-12);
    bt_json_free(j);
}

static void test_rejects(void) {
    reject("");
    reject("{");
    reject("[1,]");            /* trailing comma  */
    reject("{\"a\":1,}");
    reject("{a:1}");           /* unquoted key    */
    reject("{'a':1}");
    reject("// c\n{}");        /* comments        */
    reject("NaN");
    reject("Infinity");
    reject("01");              /* leading zero    */
    reject("1.");
    reject(".5");
    reject("+1");
    reject("1e");
    reject("0x10");
    reject("\"unterminated");
    reject("\"\\q\"");         /* bad escape      */
    reject("\"\\uD800\"");     /* lone surrogate  */
    reject("\"\\uDC00\"");
    reject("{} trailing");
    reject("tru");
}

static void test_depth_bomb(void) {
    /* The parser is recursive; the depth cap is what keeps a hostile set list
     * from turning into a stack overflow. */
    char buf[4096];
    size_t n = 0;
    for (; n < 2000; n++) buf[n] = '[';
    buf[n] = '\0';
    bt_json *j = NULL;
    int line = 0;
    BT_CHECK(bt_json_parse(buf, strlen(buf), &j, &line) != BT_OK);
    BT_CHECK(j == NULL);
}

int main(void) {
    BT_RUN(test_scalars);
    BT_RUN(test_arrays);
    BT_RUN(test_strings);
    BT_RUN(test_bom);
    BT_RUN(test_rejects);
    BT_RUN(test_depth_bomb);
    BT_REPORT();
}
