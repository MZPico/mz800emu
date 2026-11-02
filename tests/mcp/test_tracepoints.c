/*
 * test_tracepoints.c - the non-stopping tracepoints, the PSG write log and the VRAM hash
 * (src/emulator/debugger/tracepoints.c) and their MCP commands (trace_add, trace_clear, trace_read,
 * trace_config, psg_log_start, psg_log_read, psg_log_stop, vram_hash in src/emulator/mcp/dispatch.c),
 * against a fake machine (test_tracepoints_machine_stub.c). The emulator hot-loop side is covered by
 * tests/mcp/test_trace_pipe.py (the real binary).
 */

#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <json-glib/json-glib.h>

#include "emulator/mcp/dispatch.h"
#include "emulator/mcp/jsonl_io.h"
#include "emulator/debugger/tracepoints.h"
#include "test_dispatch_dbgapi_stub.h"
#include "test_tracepoints_machine_stub.h"

/* ------------------------------------------------------------------ helpers */

static JsonParser *g_parser = NULL;

/** dispatches a request line; returns the response's root object (valid until the next call) */
static JsonObject *_call(const char *line) {
    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_parse_line(line, &msg));
    char *resp = NULL;
    (void)mcp_dispatch_request(msg, &resp);
    jsonl_msg_free(msg);
    TEST_ASSERT_NOT_NULL(resp);
    if (g_parser) g_object_unref(g_parser);
    g_parser = json_parser_new();
    TEST_ASSERT_TRUE(json_parser_load_from_data(g_parser, resp, -1, NULL));
    free(resp);
    return json_node_get_object(json_parser_get_root(g_parser));
}

static bool _ok(JsonObject *o) { return json_object_get_boolean_member(o, "success"); }
static JsonObject *_data(JsonObject *o) { return json_object_get_object_member(o, "data"); }

static void _hit(uint16_t pc) {
    g_tp_fake.ticks += 20; g_tp_fake.cycles += 4;
    (void)tracepoints_on_instruction(pc);
}

void setUp(void) {
    dispatch_stub_reset();
    mcp_dispatch_init();
    tp_fake_reset();
    tracepoints_clear(0);
    tracepoints_config(TP_DEFAULT_CAPACITY, true);
    tracepoints_psg_log_stop();
}

void tearDown(void) {
}

/* ------------------------------------------------------------------ tests */

void test_crc32_is_zlib_crc32(void) {
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, tp_crc32(0, (const uint8_t *)"123456789", 9));
    /* continued in two parts = in one */
    uint32_t c = tp_crc32(0, (const uint8_t *)"1234", 4);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, tp_crc32(c, (const uint8_t *)"56789", 5));
    TEST_ASSERT_EQUAL_HEX32(0u, tp_crc32(0, NULL, 0));
}

void test_vram_hash_command(void) {
    for (int p = 0; p < 4; p++)
        for (int i = 0; i < 8192; i++) g_tp_fake.vram[p][i] = (uint8_t)(i * (p + 3));
    JsonObject *o = _call("{\"type\":\"request\",\"id\":1,\"cmd\":\"vram_hash\"}");
    TEST_ASSERT_TRUE(_ok(o));
    JsonArray *a = json_object_get_array_member(_data(o), "crc32");
    TEST_ASSERT_EQUAL_INT(4, json_array_get_length(a));
    for (int p = 0; p < 4; p++)
        TEST_ASSERT_EQUAL_UINT32(tp_crc32(0, g_tp_fake.vram[p], 8000), (uint32_t)json_array_get_int_element(a, p));
    TEST_ASSERT_EQUAL_INT(8000, json_object_get_int_member(_data(o), "len"));
    /* len: only the first bytes; bytes past it do not matter */
    o = _call("{\"type\":\"request\",\"id\":2,\"cmd\":\"vram_hash\",\"data\":{\"len\":100}}");
    a = json_object_get_array_member(_data(o), "crc32");
    TEST_ASSERT_EQUAL_UINT32(tp_crc32(0, g_tp_fake.vram[2], 100), (uint32_t)json_array_get_int_element(a, 2));
    TEST_ASSERT_FALSE(_ok(_call("{\"type\":\"request\",\"id\":3,\"cmd\":\"vram_hash\",\"data\":{\"len\":9000}}")));
    g_tp_fake.has_vram = 0;
    TEST_ASSERT_FALSE(_ok(_call("{\"type\":\"request\",\"id\":4,\"cmd\":\"vram_hash\"}")));
}

void test_trace_add_validates(void) {
    TEST_ASSERT_FALSE(_ok(_call("{\"type\":\"request\",\"id\":1,\"cmd\":\"trace_add\",\"data\":{}}")));
    TEST_ASSERT_FALSE(_ok(_call("{\"type\":\"request\",\"id\":2,\"cmd\":\"trace_add\",\"data\":{\"addr\":65536}}")));
    TEST_ASSERT_FALSE(_ok(_call("{\"type\":\"request\",\"id\":3,\"cmd\":\"trace_add\",\"data\":{\"addr\":1,\"regs\":[\"XY\"]}}")));
    TEST_ASSERT_FALSE(_ok(_call("{\"type\":\"request\",\"id\":4,\"cmd\":\"trace_add\",\"data\":{\"addr\":1,\"mem\":[[1,0]]}}")));
    TEST_ASSERT_FALSE(_ok(_call("{\"type\":\"request\",\"id\":5,\"cmd\":\"trace_add\",\"data\":{\"addr\":1,\"mem\":[[1]]}}")));
    TEST_ASSERT_FALSE(_ok(_call("{\"type\":\"request\",\"id\":6,\"cmd\":\"trace_add\",\"data\":{\"addr\":1,\"mem\":[[0,40000],[0,40000]]}}")));
    g_tp_fake.has_vram = 0;
    TEST_ASSERT_FALSE(_ok(_call("{\"type\":\"request\",\"id\":7,\"cmd\":\"trace_add\",\"data\":{\"addr\":1,\"vram_hash\":true}}")));
    TEST_ASSERT_EQUAL_UINT8(0, g_tracepoints_hot);
    JsonObject *o = _call("{\"type\":\"request\",\"id\":8,\"cmd\":\"trace_add\",\"data\":{\"addr\":1,\"regs\":[\"af\",\"HL_\"],\"mem\":[[0xFFFF,2]]}}");
    TEST_ASSERT_TRUE(_ok(o));
    TEST_ASSERT_EQUAL_INT(28 + 6 + 6 + 2, json_object_get_int_member(_data(o), "record_bytes"));
    TEST_ASSERT_EQUAL_UINT8(1, g_tracepoints_hot);
}

void test_records_registers_memory_vram_and_time(void) {
    TEST_ASSERT_TRUE(_ok(_call("{\"type\":\"request\",\"id\":1,\"cmd\":\"trace_add\",\"data\":"
                               "{\"addr\":4660,\"regs\":[\"AF\",\"PC\"],\"mem\":[[256,3],[65535,2]],\"vram_hash\":true}}")));
    g_tp_fake.regs[0] = 0x1234; g_tp_fake.regs[11] = 0x4660;
    g_tp_fake.mem[256] = 0xAA; g_tp_fake.mem[257] = 0xBB; g_tp_fake.mem[258] = 0xCC;
    g_tp_fake.mem[65535] = 0x01; g_tp_fake.mem[0] = 0x02;            /* (a range wraps at 0xFFFF) */
    g_tp_fake.vram[1][5] = 7;
    g_tp_fake.cycles = 1000; g_tp_fake.ticks = 5000; g_tp_fake.frame = 3;
    TEST_ASSERT_FALSE(tracepoints_on_instruction(4660));
    TEST_ASSERT_FALSE(tracepoints_on_instruction(4660));       /* the same instruction again (after a pause): once */
    TEST_ASSERT_FALSE(tracepoints_on_instruction(4661));       /* no tracepoint there */
    g_tp_fake.mem[256] = 0x11; g_tp_fake.ticks = 5100; g_tp_fake.cycles = 1020;
    TEST_ASSERT_FALSE(tracepoints_on_instruction(4660));
    JsonObject *o = _call("{\"type\":\"request\",\"id\":2,\"cmd\":\"trace_read\"}");
    TEST_ASSERT_TRUE(_ok(o));
    JsonObject *d = _data(o);
    TEST_ASSERT_EQUAL_INT(2, json_object_get_int_member(d, "count"));
    TEST_ASSERT_EQUAL_INT(0, json_object_get_int_member(d, "pending"));
    TEST_ASSERT_FALSE(json_object_get_boolean_member(d, "overflow"));
    TEST_ASSERT_EQUAL_INT(2, json_object_get_int_member(d, "total"));
    JsonArray *recs = json_object_get_array_member(d, "records");
    JsonObject *r0 = json_array_get_object_element(recs, 0), *r1 = json_array_get_object_element(recs, 1);
    TEST_ASSERT_EQUAL_INT(4660, json_object_get_int_member(r0, "addr"));
    TEST_ASSERT_EQUAL_INT(1000, json_object_get_int_member(r0, "cycles"));
    TEST_ASSERT_EQUAL_INT(5000, json_object_get_int_member(r0, "ticks"));
    TEST_ASSERT_EQUAL_INT(3, json_object_get_int_member(r0, "frame"));
    JsonObject *regs = json_object_get_object_member(r0, "regs");
    TEST_ASSERT_EQUAL_INT(0x1234, json_object_get_int_member(regs, "AF"));
    TEST_ASSERT_EQUAL_INT(0x4660, json_object_get_int_member(regs, "PC"));
    JsonArray *mem = json_object_get_array_member(r0, "mem");
    TEST_ASSERT_EQUAL_STRING("aabbcc", json_array_get_string_element(mem, 0));
    TEST_ASSERT_EQUAL_STRING("0102", json_array_get_string_element(mem, 1));
    TEST_ASSERT_EQUAL_STRING("11bbcc", json_array_get_string_element(json_object_get_array_member(r1, "mem"), 0));
    JsonArray *v = json_object_get_array_member(r0, "vram_crc32");
    TEST_ASSERT_EQUAL_UINT32(tp_crc32(0, g_tp_fake.vram[1], 8000), (uint32_t)json_array_get_int_element(v, 1));
    TEST_ASSERT_EQUAL_INT(1020, json_object_get_int_member(r1, "cycles"));
    /* read again: nothing */
    o = _call("{\"type\":\"request\",\"id\":3,\"cmd\":\"trace_read\",\"data\":{\"max\":5}}");
    TEST_ASSERT_EQUAL_INT(0, json_object_get_int_member(_data(o), "count"));
    TEST_ASSERT_FALSE(_ok(_call("{\"type\":\"request\",\"id\":4,\"cmd\":\"trace_read\",\"data\":{\"max\":0}}")));
}

void test_two_tracepoints_and_clear(void) {
    JsonObject *o = _call("{\"type\":\"request\",\"id\":1,\"cmd\":\"trace_add\",\"data\":{\"addr\":16}}");
    int id1 = (int)json_object_get_int_member(_data(o), "id");
    o = _call("{\"type\":\"request\",\"id\":2,\"cmd\":\"trace_add\",\"data\":{\"addr\":32,\"regs\":[\"BC\"]}}");
    int id2 = (int)json_object_get_int_member(_data(o), "id");
    TEST_ASSERT_TRUE(id2 > id1);
    _hit(16); _hit(32); _hit(16);
    o = _call("{\"type\":\"request\",\"id\":3,\"cmd\":\"trace_config\"}");
    JsonArray *tps = json_object_get_array_member(_data(o), "tracepoints");
    TEST_ASSERT_EQUAL_INT(2, json_array_get_length(tps));
    TEST_ASSERT_EQUAL_INT(2, json_object_get_int_member(json_array_get_object_element(tps, 0), "hits"));
    TEST_ASSERT_EQUAL_INT(3, json_object_get_int_member(_data(o), "pending"));
    /* remove one: its address records no more, the buffered records stay */
    char line[128];
    g_snprintf(line, sizeof(line), "{\"type\":\"request\",\"id\":4,\"cmd\":\"trace_clear\",\"data\":{\"id\":%d}}", id1);
    TEST_ASSERT_EQUAL_INT(1, json_object_get_int_member(_data(_call(line)), "cleared"));
    _hit(16); _hit(32);
    o = _call("{\"type\":\"request\",\"id\":5,\"cmd\":\"trace_read\",\"data\":{\"max\":2}}");
    TEST_ASSERT_EQUAL_INT(2, json_object_get_int_member(_data(o), "count"));
    TEST_ASSERT_EQUAL_INT(2, json_object_get_int_member(_data(o), "pending"));   /* max: the rest stays */
    TEST_ASSERT_FALSE(_ok(_call("{\"type\":\"request\",\"id\":6,\"cmd\":\"trace_clear\",\"data\":{\"id\":999}}")));
    /* clear all: the records too, the hot path off */
    TEST_ASSERT_EQUAL_INT(1, json_object_get_int_member(_data(_call("{\"type\":\"request\",\"id\":7,\"cmd\":\"trace_clear\"}")), "cleared"));
    TEST_ASSERT_EQUAL_UINT8(0, g_tracepoints_hot);
    TEST_ASSERT_EQUAL_INT(0, json_object_get_int_member(_data(_call("{\"type\":\"request\",\"id\":8,\"cmd\":\"trace_read\"}")), "count"));
}

void test_full_buffer_pauses_without_loss(void) {
    tracepoints_config(4096, true);
    TEST_ASSERT_TRUE(_ok(_call("{\"type\":\"request\",\"id\":1,\"cmd\":\"trace_add\",\"data\":{\"addr\":7,\"mem\":[[0,100]]}}")));
    int n = 0, paused_at = -1;
    for (int i = 0; i < 100 && paused_at < 0; i++) {
        g_tp_fake.ticks += 20;
        n++;
        if (tracepoints_on_instruction(7)) paused_at = n;
    }
    TEST_ASSERT_TRUE(paused_at > 0);
    TEST_ASSERT_EQUAL_INT(1, g_tp_fake.pauses);
    /* each record taken before the pause is there */
    JsonObject *d = _data(_call("{\"type\":\"request\",\"id\":2,\"cmd\":\"trace_read\",\"data\":{\"max\":1000}}"));
    TEST_ASSERT_EQUAL_INT(paused_at, json_object_get_int_member(d, "count"));
    TEST_ASSERT_FALSE(json_object_get_boolean_member(d, "overflow"));
    TEST_ASSERT_EQUAL_INT(0, json_object_get_int_member(d, "dropped"));
    TEST_ASSERT_EQUAL_STRING("pause", json_object_get_string_member(d, "on_full"));
}

void test_full_buffer_drop_reports_overflow(void) {
    TEST_ASSERT_TRUE(_ok(_call("{\"type\":\"request\",\"id\":1,\"cmd\":\"trace_config\",\"data\":{\"capacity\":4096,\"on_full\":\"drop\"}}")));
    TEST_ASSERT_FALSE(_ok(_call("{\"type\":\"request\",\"id\":2,\"cmd\":\"trace_config\",\"data\":{\"on_full\":\"wait\"}}")));
    TEST_ASSERT_TRUE(_ok(_call("{\"type\":\"request\",\"id\":3,\"cmd\":\"trace_add\",\"data\":{\"addr\":7,\"mem\":[[0,100]]}}")));
    for (int i = 0; i < 100; i++) { g_tp_fake.ticks += 20; TEST_ASSERT_FALSE(tracepoints_on_instruction(7)); }
    TEST_ASSERT_EQUAL_INT(0, g_tp_fake.pauses);
    JsonObject *d = _data(_call("{\"type\":\"request\",\"id\":4,\"cmd\":\"trace_read\",\"data\":{\"max\":1000}}"));
    int kept = (int)json_object_get_int_member(d, "count");
    TEST_ASSERT_EQUAL_INT(4096 / (28 + 106), kept);
    TEST_ASSERT_TRUE(json_object_get_boolean_member(d, "overflow"));
    TEST_ASSERT_EQUAL_INT(100 - kept, json_object_get_int_member(d, "dropped"));
    TEST_ASSERT_EQUAL_INT(100, json_object_get_int_member(d, "total"));
    /* reported once: the next read is clean */
    d = _data(_call("{\"type\":\"request\",\"id\":5,\"cmd\":\"trace_read\"}"));
    TEST_ASSERT_FALSE(json_object_get_boolean_member(d, "overflow"));
    TEST_ASSERT_EQUAL_INT(0, json_object_get_int_member(d, "dropped"));
}

void test_psg_log(void) {
    tracepoints_psg_write(0x9F, 1, 100);          /* not started: nothing */
    JsonObject *o = _call("{\"type\":\"request\",\"id\":1,\"cmd\":\"psg_log_start\"}");
    TEST_ASSERT_TRUE(_ok(o));
    TEST_ASSERT_EQUAL_UINT8(1, g_tp_psg_log_active);
    g_tp_fake.frame = 9;
    tracepoints_psg_write(0x9F, 1, 1000);
    tracepoints_psg_write(0x05, 1, 1055);
    o = _call("{\"type\":\"request\",\"id\":2,\"cmd\":\"psg_log_stop\"}");
    TEST_ASSERT_EQUAL_INT(2, json_object_get_int_member(_data(o), "pending"));
    tracepoints_psg_write(0xBF, 1, 2000);         /* stopped: not logged */
    o = _call("{\"type\":\"request\",\"id\":3,\"cmd\":\"psg_log_read\"}");
    JsonObject *d = _data(o);
    TEST_ASSERT_EQUAL_INT(2, json_object_get_int_member(d, "count"));
    TEST_ASSERT_FALSE(json_object_get_boolean_member(d, "active"));
    JsonArray *w = json_object_get_array_member(d, "writes");
    JsonArray *w1 = json_array_get_array_element(w, 1);
    TEST_ASSERT_EQUAL_INT(1055, json_array_get_int_element(w1, 0));   /* ticks */
    TEST_ASSERT_EQUAL_INT(211, json_array_get_int_element(w1, 1));    /* cycles = ticks / 5 */
    TEST_ASSERT_EQUAL_INT(9, json_array_get_int_element(w1, 2));      /* frame */
    TEST_ASSERT_EQUAL_INT(5, json_array_get_int_element(w1, 3));      /* value */
    TEST_ASSERT_EQUAL_INT(1, json_array_get_int_element(w1, 4));      /* channel */
}

void test_psg_log_full(void) {
    /* drop: the writes that do not fit are counted */
    TEST_ASSERT_TRUE(_ok(_call("{\"type\":\"request\",\"id\":1,\"cmd\":\"psg_log_start\",\"data\":{\"capacity\":2048,\"on_full\":\"drop\"}}")));
    for (int i = 0; i < 3000; i++) tracepoints_psg_write((uint8_t)i, 1, (uint64_t)i);
    JsonObject *d = _data(_call("{\"type\":\"request\",\"id\":2,\"cmd\":\"psg_log_read\",\"data\":{\"max\":5000}}"));
    TEST_ASSERT_EQUAL_INT(2048, json_object_get_int_member(d, "count"));
    TEST_ASSERT_TRUE(json_object_get_boolean_member(d, "overflow"));
    TEST_ASSERT_EQUAL_INT(952, json_object_get_int_member(d, "dropped"));
    /* pause: requested a reserve before the end; the hot loop pauses at the next instruction */
    TEST_ASSERT_TRUE(_ok(_call("{\"type\":\"request\",\"id\":3,\"cmd\":\"psg_log_start\",\"data\":{\"capacity\":2048}}")));
    TEST_ASSERT_EQUAL_UINT8(0, g_tracepoints_hot);
    for (int i = 0; i < 1100; i++) tracepoints_psg_write(1, 1, (uint64_t)i);
    TEST_ASSERT_EQUAL_UINT8(1, g_tracepoints_hot);
    TEST_ASSERT_TRUE(tracepoints_on_instruction(0));
    TEST_ASSERT_EQUAL_INT(1, g_tp_fake.pauses);
    TEST_ASSERT_EQUAL_UINT8(0, g_tracepoints_hot);
    d = _data(_call("{\"type\":\"request\",\"id\":4,\"cmd\":\"psg_log_read\",\"data\":{\"max\":5000}}"));
    TEST_ASSERT_EQUAL_INT(1100, json_object_get_int_member(d, "count"));
    TEST_ASSERT_FALSE(json_object_get_boolean_member(d, "overflow"));
    tracepoints_psg_log_stop();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_crc32_is_zlib_crc32);
    RUN_TEST(test_vram_hash_command);
    RUN_TEST(test_trace_add_validates);
    RUN_TEST(test_records_registers_memory_vram_and_time);
    RUN_TEST(test_two_tracepoints_and_clear);
    RUN_TEST(test_full_buffer_pauses_without_loss);
    RUN_TEST(test_full_buffer_drop_reports_overflow);
    RUN_TEST(test_psg_log);
    RUN_TEST(test_psg_log_full);
    return UNITY_END();
}
