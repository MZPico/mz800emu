/**
 * @file   test_jsonl_parser.c
 * @brief  Unity testy pro JSONL transport core (V0.A.2).
 *
 * Pokrývá:
 *   - parser klasifikace 6 typů zpráv (REQUEST, RESPONSE, EVENT, TRAP,
 *     TRAP_RESPONSE, HELLO) podle pole `type` i fingerprint fallback
 *   - encoder builders pro všech 6 typů + roundtrip (build -> parse ->
 *     ověř fieldy)
 *   - edge cases: prázdný řádek, invalid JSON, JSON který není objekt,
 *     objekt bez schématu, NULL pointery
 *   - stream I/O: zápis N řádků do tmpfile, čtení zpět, ověření EOF
 *
 * Standalone test - linkuje pouze `jsonl_io.c` přímo (s
 * `MZ800EMU_MCP_TEST_BUILD` aby se nezpráhal `mzarch_config.h`)
 * + Unity + json-glib + glib. Žádný emu core.
 *
 * @par Licence:
 * GPL-3.0-or-later. Viz licence header v jsonl_io.c.
 */

#include "unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <json-glib/json-glib.h>

#include "emulator/mcp/jsonl_io.h"


/** @brief Inicializace před každým testem - JSONL nemá globální stav. */
void setUp(void) {
}


/** @brief Úklid po každém testu. */
void tearDown(void) {
}


/* ====================================================================== */
/* Parser - klasifikace podle pole `type`                                  */
/* ====================================================================== */

void test_parse_request_by_type(void) {
    const char *line = "{\"type\":\"request\",\"req_id\":42,\"cmd\":\"ping\"}";
    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_parse_line(line, &msg));
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_EQUAL_INT(JSONL_MSG_REQUEST, jsonl_msg_get_type(msg));
    TEST_ASSERT_EQUAL_INT64(42, jsonl_msg_get_req_id(msg));
    TEST_ASSERT_EQUAL_STRING("ping", jsonl_msg_get_cmd(msg));
    jsonl_msg_free(msg);
}


void test_parse_response_by_type(void) {
    const char *line = "{\"type\":\"response\",\"req_id\":7,\"success\":true}";
    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_parse_line(line, &msg));
    TEST_ASSERT_EQUAL_INT(JSONL_MSG_RESPONSE, jsonl_msg_get_type(msg));
    TEST_ASSERT_EQUAL_INT64(7, jsonl_msg_get_req_id(msg));
    TEST_ASSERT_TRUE(jsonl_msg_get_success(msg));
    jsonl_msg_free(msg);
}


void test_parse_event(void) {
    const char *line = "{\"type\":\"event\",\"topic\":\"emu.paused\"}";
    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_parse_line(line, &msg));
    TEST_ASSERT_EQUAL_INT(JSONL_MSG_EVENT, jsonl_msg_get_type(msg));
    TEST_ASSERT_EQUAL_STRING("emu.paused", jsonl_msg_get_topic(msg));
    jsonl_msg_free(msg);
}


void test_parse_trap(void) {
    const char *line =
        "{\"type\":\"trap\",\"trap_id\":13,\"topic\":\"breakpoint.hit\","
        "\"timeout_ms\":5000}";
    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_parse_line(line, &msg));
    TEST_ASSERT_EQUAL_INT(JSONL_MSG_TRAP, jsonl_msg_get_type(msg));
    TEST_ASSERT_EQUAL_INT64(13, jsonl_msg_get_trap_id(msg));
    TEST_ASSERT_EQUAL_STRING("breakpoint.hit", jsonl_msg_get_topic(msg));
    TEST_ASSERT_EQUAL_INT64(5000, jsonl_msg_get_timeout_ms(msg));
    jsonl_msg_free(msg);
}


void test_parse_trap_response(void) {
    const char *line =
        "{\"type\":\"trap_response\",\"trap_id\":13,\"action\":\"continue\"}";
    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_parse_line(line, &msg));
    TEST_ASSERT_EQUAL_INT(JSONL_MSG_TRAP_RESPONSE, jsonl_msg_get_type(msg));
    TEST_ASSERT_EQUAL_INT64(13, jsonl_msg_get_trap_id(msg));
    TEST_ASSERT_EQUAL_STRING("continue", jsonl_msg_get_action(msg));
    jsonl_msg_free(msg);
}


void test_parse_hello(void) {
    const char *line =
        "{\"type\":\"hello\",\"version\":\"1.0\","
        "\"commands\":[\"ping\",\"step\"]}";
    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_parse_line(line, &msg));
    TEST_ASSERT_EQUAL_INT(JSONL_MSG_HELLO, jsonl_msg_get_type(msg));
    jsonl_msg_free(msg);
}


/* ====================================================================== */
/* Parser - fingerprint fallback (bez pole `type`)                         */
/* ====================================================================== */

void test_parse_request_by_fingerprint(void) {
    /* Bez "type", má req_id+cmd -> REQUEST. */
    const char *line = "{\"req_id\":1,\"cmd\":\"ping\"}";
    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_parse_line(line, &msg));
    TEST_ASSERT_EQUAL_INT(JSONL_MSG_REQUEST, jsonl_msg_get_type(msg));
    jsonl_msg_free(msg);
}


void test_parse_response_by_fingerprint(void) {
    /* Bez "type", má req_id+success -> RESPONSE. */
    const char *line = "{\"req_id\":1,\"success\":false,\"error\":\"oops\"}";
    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_parse_line(line, &msg));
    TEST_ASSERT_EQUAL_INT(JSONL_MSG_RESPONSE, jsonl_msg_get_type(msg));
    TEST_ASSERT_FALSE(jsonl_msg_get_success(msg));
    TEST_ASSERT_EQUAL_STRING("oops", jsonl_msg_get_error(msg));
    jsonl_msg_free(msg);
}


/* ====================================================================== */
/* Edge cases - parser                                                     */
/* ====================================================================== */

void test_parse_empty_line(void) {
    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_ERR_PARSE, jsonl_parse_line("", &msg));
    TEST_ASSERT_NULL(msg);
}


void test_parse_whitespace_only(void) {
    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_ERR_PARSE, jsonl_parse_line("   \t", &msg));
    TEST_ASSERT_NULL(msg);
}


void test_parse_invalid_json(void) {
    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_ERR_PARSE,
                          jsonl_parse_line("{not json}", &msg));
    TEST_ASSERT_NULL(msg);
}


void test_parse_json_not_object(void) {
    /* Pole JSON value, ne objekt -> SCHEMA error. */
    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_ERR_SCHEMA,
                          jsonl_parse_line("[1,2,3]", &msg));
    TEST_ASSERT_NULL(msg);
}


void test_parse_object_unknown_schema(void) {
    /* Objekt bez `type` a bez fingerprint polí -> SCHEMA error. */
    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_ERR_SCHEMA,
                          jsonl_parse_line("{\"foo\":\"bar\"}", &msg));
    TEST_ASSERT_NULL(msg);
}


void test_parse_unknown_type_string(void) {
    /* Pole `type` má neznámou hodnotu -> SCHEMA error. */
    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_ERR_SCHEMA,
                          jsonl_parse_line("{\"type\":\"weird\"}", &msg));
    TEST_ASSERT_NULL(msg);
}


void test_parse_null_args(void) {
    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_ERR_PARSE, jsonl_parse_line(NULL, &msg));
    TEST_ASSERT_EQUAL_INT(JSONL_ERR_PARSE, jsonl_parse_line("{}", NULL));
}


void test_msg_free_null_safe(void) {
    /* Volání s NULL nesmí padnout. */
    jsonl_msg_free(NULL);
}


/* ====================================================================== */
/* Encoder builders - sanity + roundtrip                                   */
/* ====================================================================== */

void test_build_request_basic(void) {
    char *line = jsonl_build_request(1, "ping", NULL);
    TEST_ASSERT_NOT_NULL(line);
    /* Single-line: žádný newline uvnitř. */
    TEST_ASSERT_NULL(strchr(line, '\n'));

    /* Roundtrip - parse zpět a ověř. */
    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_parse_line(line, &msg));
    TEST_ASSERT_EQUAL_INT(JSONL_MSG_REQUEST, jsonl_msg_get_type(msg));
    TEST_ASSERT_EQUAL_INT64(1, jsonl_msg_get_req_id(msg));
    TEST_ASSERT_EQUAL_STRING("ping", jsonl_msg_get_cmd(msg));
    jsonl_msg_free(msg);
    g_free(line);
}


void test_build_request_rejects_empty_cmd(void) {
    TEST_ASSERT_NULL(jsonl_build_request(1, NULL, NULL));
    TEST_ASSERT_NULL(jsonl_build_request(1, "", NULL));
}


void test_build_response_success(void) {
    char *line = jsonl_build_response(7, true, NULL, NULL);
    TEST_ASSERT_NOT_NULL(line);

    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_parse_line(line, &msg));
    TEST_ASSERT_EQUAL_INT(JSONL_MSG_RESPONSE, jsonl_msg_get_type(msg));
    TEST_ASSERT_EQUAL_INT64(7, jsonl_msg_get_req_id(msg));
    TEST_ASSERT_TRUE(jsonl_msg_get_success(msg));
    TEST_ASSERT_NULL(jsonl_msg_get_error(msg));
    jsonl_msg_free(msg);
    g_free(line);
}


void test_build_response_error(void) {
    char *line = jsonl_build_response(7, false, NULL, "unknown command");
    TEST_ASSERT_NOT_NULL(line);

    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_parse_line(line, &msg));
    TEST_ASSERT_EQUAL_INT(JSONL_MSG_RESPONSE, jsonl_msg_get_type(msg));
    TEST_ASSERT_FALSE(jsonl_msg_get_success(msg));
    TEST_ASSERT_EQUAL_STRING("unknown command", jsonl_msg_get_error(msg));
    jsonl_msg_free(msg);
    g_free(line);
}


void test_build_response_rejects_error_without_message(void) {
    /* success=false vyžaduje error_msg. */
    TEST_ASSERT_NULL(jsonl_build_response(7, false, NULL, NULL));
    TEST_ASSERT_NULL(jsonl_build_response(7, false, NULL, ""));
}


void test_build_event_basic(void) {
    char *line = jsonl_build_event("memory.write", NULL);
    TEST_ASSERT_NOT_NULL(line);

    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_parse_line(line, &msg));
    TEST_ASSERT_EQUAL_INT(JSONL_MSG_EVENT, jsonl_msg_get_type(msg));
    TEST_ASSERT_EQUAL_STRING("memory.write", jsonl_msg_get_topic(msg));
    jsonl_msg_free(msg);
    g_free(line);
}


void test_build_event_rejects_empty_topic(void) {
    TEST_ASSERT_NULL(jsonl_build_event(NULL, NULL));
    TEST_ASSERT_NULL(jsonl_build_event("", NULL));
}


void test_build_trap_basic(void) {
    char *line = jsonl_build_trap(13, "breakpoint.hit", NULL, 5000);
    TEST_ASSERT_NOT_NULL(line);

    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_parse_line(line, &msg));
    TEST_ASSERT_EQUAL_INT(JSONL_MSG_TRAP, jsonl_msg_get_type(msg));
    TEST_ASSERT_EQUAL_INT64(13, jsonl_msg_get_trap_id(msg));
    TEST_ASSERT_EQUAL_STRING("breakpoint.hit", jsonl_msg_get_topic(msg));
    TEST_ASSERT_EQUAL_INT64(5000, jsonl_msg_get_timeout_ms(msg));
    jsonl_msg_free(msg);
    g_free(line);
}


void test_build_trap_response_basic(void) {
    char *line = jsonl_build_trap_response(13, "continue");
    TEST_ASSERT_NOT_NULL(line);

    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_parse_line(line, &msg));
    TEST_ASSERT_EQUAL_INT(JSONL_MSG_TRAP_RESPONSE, jsonl_msg_get_type(msg));
    TEST_ASSERT_EQUAL_INT64(13, jsonl_msg_get_trap_id(msg));
    TEST_ASSERT_EQUAL_STRING("continue", jsonl_msg_get_action(msg));
    jsonl_msg_free(msg);
    g_free(line);
}


void test_build_trap_response_rejects_empty(void) {
    TEST_ASSERT_NULL(jsonl_build_trap_response(1, NULL));
    TEST_ASSERT_NULL(jsonl_build_trap_response(1, ""));
}


void test_build_hello_basic(void) {
    const char *const cmds[] = { "ping", "step", "continue", NULL };
    char *line = jsonl_build_hello("1.0", NULL, cmds);
    TEST_ASSERT_NOT_NULL(line);

    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_parse_line(line, &msg));
    TEST_ASSERT_EQUAL_INT(JSONL_MSG_HELLO, jsonl_msg_get_type(msg));
    jsonl_msg_free(msg);
    g_free(line);
}


void test_build_hello_no_commands(void) {
    /* NULL commands -> prázdné pole, validní HELLO. */
    char *line = jsonl_build_hello("1.0", NULL, NULL);
    TEST_ASSERT_NOT_NULL(line);

    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_parse_line(line, &msg));
    TEST_ASSERT_EQUAL_INT(JSONL_MSG_HELLO, jsonl_msg_get_type(msg));
    jsonl_msg_free(msg);
    g_free(line);
}


/* ====================================================================== */
/* Data sub-tree - roundtrip s payloadem                                   */
/* ====================================================================== */

void test_request_with_data_payload(void) {
    /* Sestavíme JsonNode obsahující objekt {"addr":4096,"value":127}. */
    JsonBuilder *b = json_builder_new();
    json_builder_begin_object(b);
    json_builder_set_member_name(b, "addr");
    json_builder_add_int_value(b, 4096);
    json_builder_set_member_name(b, "value");
    json_builder_add_int_value(b, 127);
    json_builder_end_object(b);
    JsonNode *data_node = json_builder_get_root(b);

    char *line = jsonl_build_request(99, "memory.write", data_node);
    TEST_ASSERT_NOT_NULL(line);

    /* Parse zpět, ověř že data jsou tam. */
    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_parse_line(line, &msg));
    void *parsed_data = jsonl_msg_get_data_node(msg);
    TEST_ASSERT_NOT_NULL(parsed_data);

    JsonNode *pd = (JsonNode *) parsed_data;
    TEST_ASSERT_EQUAL_INT(JSON_NODE_OBJECT, json_node_get_node_type(pd));
    JsonObject *po = json_node_get_object(pd);
    TEST_ASSERT_EQUAL_INT64(4096,
        json_object_get_int_member(po, "addr"));
    TEST_ASSERT_EQUAL_INT64(127,
        json_object_get_int_member(po, "value"));

    jsonl_msg_free(msg);
    g_free(line);
    json_node_free(data_node);
    g_object_unref(b);
}


/* ====================================================================== */
/* Stream I/O - read/write nad tmpfile                                     */
/* ====================================================================== */

void test_stream_write_read_three_lines(void) {
    FILE *fp = tmpfile();
    TEST_ASSERT_NOT_NULL(fp);

    /* Zapíšeme 3 řádky. */
    TEST_ASSERT_EQUAL_INT(JSONL_OK,
        jsonl_write_line(fp, "{\"type\":\"event\",\"topic\":\"a\"}"));
    TEST_ASSERT_EQUAL_INT(JSONL_OK,
        jsonl_write_line(fp, "{\"type\":\"event\",\"topic\":\"b\"}"));
    TEST_ASSERT_EQUAL_INT(JSONL_OK,
        jsonl_write_line(fp, "{\"type\":\"event\",\"topic\":\"c\"}"));

    /* Rewind a čteme zpět. */
    rewind(fp);

    char *line = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_read_line(fp, &line));
    TEST_ASSERT_NOT_NULL(strstr(line, "\"topic\":\"a\""));
    free(line);

    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_read_line(fp, &line));
    TEST_ASSERT_NOT_NULL(strstr(line, "\"topic\":\"b\""));
    free(line);

    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_read_line(fp, &line));
    TEST_ASSERT_NOT_NULL(strstr(line, "\"topic\":\"c\""));
    free(line);

    /* 4. čtení = EOF. */
    TEST_ASSERT_EQUAL_INT(JSONL_ERR_EOF, jsonl_read_line(fp, &line));
    TEST_ASSERT_NULL(line);

    fclose(fp);
}


void test_stream_read_strips_crlf(void) {
    /* Manuálně zapíšeme řádek s CRLF aby simuloval Windows příjem. */
    FILE *fp = tmpfile();
    TEST_ASSERT_NOT_NULL(fp);
    fputs("{\"type\":\"event\",\"topic\":\"x\"}\r\n", fp);
    rewind(fp);

    char *line = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_read_line(fp, &line));
    /* Žádný CR ani LF v output - reader to odřízl. */
    TEST_ASSERT_NULL(strchr(line, '\r'));
    TEST_ASSERT_NULL(strchr(line, '\n'));
    /* Parser musí přijmout. */
    st_JSONL_MESSAGE *msg = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_OK, jsonl_parse_line(line, &msg));
    jsonl_msg_free(msg);
    free(line);
    fclose(fp);
}


void test_stream_read_null_args(void) {
    char *line = NULL;
    TEST_ASSERT_EQUAL_INT(JSONL_ERR_IO, jsonl_read_line(NULL, &line));
    /* `out_line` NULL - také IO error. */
    FILE *fp = tmpfile();
    TEST_ASSERT_EQUAL_INT(JSONL_ERR_IO, jsonl_read_line(fp, NULL));
    fclose(fp);
}


void test_stream_write_null_args(void) {
    TEST_ASSERT_EQUAL_INT(JSONL_ERR_IO, jsonl_write_line(NULL, "x"));
    FILE *fp = tmpfile();
    TEST_ASSERT_EQUAL_INT(JSONL_ERR_IO, jsonl_write_line(fp, NULL));
    fclose(fp);
}


/* ====================================================================== */
/* Main runner                                                             */
/* ====================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* Parser - klasifikace podle pole `type` */
    RUN_TEST(test_parse_request_by_type);
    RUN_TEST(test_parse_response_by_type);
    RUN_TEST(test_parse_event);
    RUN_TEST(test_parse_trap);
    RUN_TEST(test_parse_trap_response);
    RUN_TEST(test_parse_hello);

    /* Parser - fingerprint fallback */
    RUN_TEST(test_parse_request_by_fingerprint);
    RUN_TEST(test_parse_response_by_fingerprint);

    /* Parser - edge cases */
    RUN_TEST(test_parse_empty_line);
    RUN_TEST(test_parse_whitespace_only);
    RUN_TEST(test_parse_invalid_json);
    RUN_TEST(test_parse_json_not_object);
    RUN_TEST(test_parse_object_unknown_schema);
    RUN_TEST(test_parse_unknown_type_string);
    RUN_TEST(test_parse_null_args);
    RUN_TEST(test_msg_free_null_safe);

    /* Encoder + roundtrip */
    RUN_TEST(test_build_request_basic);
    RUN_TEST(test_build_request_rejects_empty_cmd);
    RUN_TEST(test_build_response_success);
    RUN_TEST(test_build_response_error);
    RUN_TEST(test_build_response_rejects_error_without_message);
    RUN_TEST(test_build_event_basic);
    RUN_TEST(test_build_event_rejects_empty_topic);
    RUN_TEST(test_build_trap_basic);
    RUN_TEST(test_build_trap_response_basic);
    RUN_TEST(test_build_trap_response_rejects_empty);
    RUN_TEST(test_build_hello_basic);
    RUN_TEST(test_build_hello_no_commands);

    /* Data payload */
    RUN_TEST(test_request_with_data_payload);

    /* Stream I/O */
    RUN_TEST(test_stream_write_read_three_lines);
    RUN_TEST(test_stream_read_strips_crlf);
    RUN_TEST(test_stream_read_null_args);
    RUN_TEST(test_stream_write_null_args);

    return UNITY_END();
}
