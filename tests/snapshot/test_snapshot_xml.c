/*
 * test_snapshot_xml.c — unit testy pro snapshot XML serializaci
 *
 * Testuje: snapshot_xml_writer_*, snapshot_xml_reader_*,
 *          roundtrip hodnot přes XML writer → reader.
 *
 * Licence: GPLv3
 */

#include "mztest.h"
#include <glib.h>
#include <string.h>

#include "emulator/snapshot/snapshot_xml.h"

void setUp(void) { }
void tearDown(void) { }

/* ================================================================
 * SMOKE TESTY
 * ================================================================ */

/* vytvoření writeru a získání XML */
void test_xml_writer_create(void)
{
    snapshot_xml_writer_t *w = snapshot_xml_writer_new();
    TEST_ASSERT_NOT_NULL(w);

    snapshot_xml_write_header(w);
    char *xml = snapshot_xml_writer_finish(w);
    TEST_ASSERT_NOT_NULL(xml);

    /* výstup musí obsahovat XML hlavičku */
    TEST_ASSERT_NOT_NULL(strstr(xml, "<?xml"));

    g_free(xml);
}

/* parsování prázdného XML elementu */
void test_xml_reader_create(void)
{
    const char *xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<root></root>";

    snapshot_xml_reader_t *r = snapshot_xml_reader_new(xml);
    TEST_ASSERT_NOT_NULL(r);

    snapshot_xml_reader_free(r);
}

/* parsování nevalidního XML → NULL */
void test_xml_reader_invalid(void)
{
    snapshot_xml_reader_t *r = snapshot_xml_reader_new("<unclosed");
    TEST_ASSERT_NULL_MESSAGE(r, "Nevalidní XML by mělo vrátit NULL");
}

/* ================================================================
 * UNIT TESTY — hodnoty roundtrip
 * ================================================================ */

/* helper: zapíše hodnoty do XML writeru, vrátí XML string */
static char *write_test_values(void)
{
    snapshot_xml_writer_t *w = snapshot_xml_writer_new();
    snapshot_xml_write_header(w);

    snapshot_xml_open_element(w, "test_data");

    snapshot_xml_write_string(w, "name", "test_value");
    snapshot_xml_write_int(w, "negative", -42);
    snapshot_xml_write_uint(w, "positive", 12345);
    snapshot_xml_write_hex8(w, "byte", 0xAB);
    snapshot_xml_write_hex16(w, "word", 0xDEAD);
    snapshot_xml_write_hex32(w, "dword", 0xCAFEBABE);
    snapshot_xml_write_uint64(w, "big", 9876543210ULL);
    snapshot_xml_write_bool(w, "flag_true", true);
    snapshot_xml_write_bool(w, "flag_false", false);

    snapshot_xml_close_element(w);

    return snapshot_xml_writer_finish(w);
}

/* roundtrip string */
void test_xml_roundtrip_string(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    char *xml = write_test_values();
    snapshot_xml_reader_t *r = snapshot_xml_reader_new(xml);
    TEST_ASSERT_NOT_NULL(r);

    TEST_ASSERT_TRUE(snapshot_xml_enter_element(r, "test_data"));

    char *val = NULL;
    TEST_ASSERT_TRUE(snapshot_xml_read_string(r, "name", &val));
    TEST_ASSERT_EQUAL_STRING("test_value", val);
    g_free(val);

    snapshot_xml_leave_element(r);
    snapshot_xml_reader_free(r);
    g_free(xml);
}

/* roundtrip int (záporná hodnota) */
void test_xml_roundtrip_int(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    char *xml = write_test_values();
    snapshot_xml_reader_t *r = snapshot_xml_reader_new(xml);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(snapshot_xml_enter_element(r, "test_data"));

    int val = 0;
    TEST_ASSERT_TRUE(snapshot_xml_read_int(r, "negative", &val));
    TEST_ASSERT_EQUAL_INT(-42, val);

    snapshot_xml_leave_element(r);
    snapshot_xml_reader_free(r);
    g_free(xml);
}

/* roundtrip uint */
void test_xml_roundtrip_uint(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    char *xml = write_test_values();
    snapshot_xml_reader_t *r = snapshot_xml_reader_new(xml);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(snapshot_xml_enter_element(r, "test_data"));

    unsigned val = 0;
    TEST_ASSERT_TRUE(snapshot_xml_read_uint(r, "positive", &val));
    TEST_ASSERT_EQUAL_UINT(12345, val);

    snapshot_xml_leave_element(r);
    snapshot_xml_reader_free(r);
    g_free(xml);
}

/* roundtrip hex8 */
void test_xml_roundtrip_hex8(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    char *xml = write_test_values();
    snapshot_xml_reader_t *r = snapshot_xml_reader_new(xml);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(snapshot_xml_enter_element(r, "test_data"));

    uint8_t val = 0;
    TEST_ASSERT_TRUE(snapshot_xml_read_hex8(r, "byte", &val));
    TEST_ASSERT_EQUAL_HEX8(0xAB, val);

    snapshot_xml_leave_element(r);
    snapshot_xml_reader_free(r);
    g_free(xml);
}

/* roundtrip hex16 */
void test_xml_roundtrip_hex16(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    char *xml = write_test_values();
    snapshot_xml_reader_t *r = snapshot_xml_reader_new(xml);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(snapshot_xml_enter_element(r, "test_data"));

    uint16_t val = 0;
    TEST_ASSERT_TRUE(snapshot_xml_read_hex16(r, "word", &val));
    TEST_ASSERT_EQUAL_HEX16(0xDEAD, val);

    snapshot_xml_leave_element(r);
    snapshot_xml_reader_free(r);
    g_free(xml);
}

/* roundtrip hex32 */
void test_xml_roundtrip_hex32(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    char *xml = write_test_values();
    snapshot_xml_reader_t *r = snapshot_xml_reader_new(xml);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(snapshot_xml_enter_element(r, "test_data"));

    uint32_t val = 0;
    TEST_ASSERT_TRUE(snapshot_xml_read_hex32(r, "dword", &val));
    TEST_ASSERT_EQUAL_HEX32(0xCAFEBABE, val);

    snapshot_xml_leave_element(r);
    snapshot_xml_reader_free(r);
    g_free(xml);
}

/* roundtrip uint64 */
void test_xml_roundtrip_uint64(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    char *xml = write_test_values();
    snapshot_xml_reader_t *r = snapshot_xml_reader_new(xml);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(snapshot_xml_enter_element(r, "test_data"));

    uint64_t val = 0;
    TEST_ASSERT_TRUE(snapshot_xml_read_uint64(r, "big", &val));
    TEST_ASSERT_EQUAL_UINT64(9876543210ULL, val);

    snapshot_xml_leave_element(r);
    snapshot_xml_reader_free(r);
    g_free(xml);
}

/* roundtrip bool */
void test_xml_roundtrip_bool(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    char *xml = write_test_values();
    snapshot_xml_reader_t *r = snapshot_xml_reader_new(xml);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(snapshot_xml_enter_element(r, "test_data"));

    bool val_true = false, val_false = true;
    TEST_ASSERT_TRUE(snapshot_xml_read_bool(r, "flag_true", &val_true));
    TEST_ASSERT_TRUE(snapshot_xml_read_bool(r, "flag_false", &val_false));
    TEST_ASSERT_TRUE(val_true);
    TEST_ASSERT_FALSE(val_false);

    snapshot_xml_leave_element(r);
    snapshot_xml_reader_free(r);
    g_free(xml);
}

/* čtení neexistujícího elementu → false */
void test_xml_read_missing_element(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    char *xml = write_test_values();
    snapshot_xml_reader_t *r = snapshot_xml_reader_new(xml);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(snapshot_xml_enter_element(r, "test_data"));

    int val = 999;
    TEST_ASSERT_FALSE(snapshot_xml_read_int(r, "neexistuje", &val));
    TEST_ASSERT_EQUAL_INT(999, val); /* hodnota se nemění */

    snapshot_xml_leave_element(r);
    snapshot_xml_reader_free(r);
    g_free(xml);
}

/* navigace — enter neexistující element → false */
void test_xml_enter_missing_element(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    char *xml = write_test_values();
    snapshot_xml_reader_t *r = snapshot_xml_reader_new(xml);
    TEST_ASSERT_NOT_NULL(r);

    TEST_ASSERT_FALSE(snapshot_xml_enter_element(r, "neexistujici_element"));

    snapshot_xml_reader_free(r);
    g_free(xml);
}

/* vnořené elementy */
void test_xml_nested_elements(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    /* zapsat vnořenou strukturu */
    snapshot_xml_writer_t *w = snapshot_xml_writer_new();
    snapshot_xml_write_header(w);

    snapshot_xml_open_element(w, "outer");
    snapshot_xml_write_int(w, "outer_val", 1);

    snapshot_xml_open_element(w, "inner");
    snapshot_xml_write_int(w, "inner_val", 2);
    snapshot_xml_close_element(w); /* inner */

    snapshot_xml_close_element(w); /* outer */

    char *xml = snapshot_xml_writer_finish(w);

    /* přečíst */
    snapshot_xml_reader_t *r = snapshot_xml_reader_new(xml);
    TEST_ASSERT_NOT_NULL(r);

    TEST_ASSERT_TRUE(snapshot_xml_enter_element(r, "outer"));

    int val = 0;
    TEST_ASSERT_TRUE(snapshot_xml_read_int(r, "outer_val", &val));
    TEST_ASSERT_EQUAL_INT(1, val);

    TEST_ASSERT_TRUE(snapshot_xml_enter_element(r, "inner"));
    TEST_ASSERT_TRUE(snapshot_xml_read_int(r, "inner_val", &val));
    TEST_ASSERT_EQUAL_INT(2, val);
    snapshot_xml_leave_element(r); /* inner */

    snapshot_xml_leave_element(r); /* outer */

    snapshot_xml_reader_free(r);
    g_free(xml);
}

/* element s atributem */
void test_xml_element_with_attribute(void)
{
    MZTEST_REQUIRE_LEVEL(MZTEST_LEVEL_UNIT);

    snapshot_xml_writer_t *w = snapshot_xml_writer_new();
    snapshot_xml_write_header(w);

    snapshot_xml_open_element_attr(w, "tagged", "type", "test_type");
    snapshot_xml_write_int(w, "val", 42);
    snapshot_xml_close_element(w);

    char *xml = snapshot_xml_writer_finish(w);

    snapshot_xml_reader_t *r = snapshot_xml_reader_new(xml);
    TEST_ASSERT_NOT_NULL(r);

    TEST_ASSERT_TRUE(snapshot_xml_enter_element(r, "tagged"));

    char *attr = NULL;
    TEST_ASSERT_TRUE(snapshot_xml_read_attr(r, "type", &attr));
    TEST_ASSERT_EQUAL_STRING("test_type", attr);
    g_free(attr);

    int val = 0;
    TEST_ASSERT_TRUE(snapshot_xml_read_int(r, "val", &val));
    TEST_ASSERT_EQUAL_INT(42, val);

    snapshot_xml_leave_element(r);
    snapshot_xml_reader_free(r);
    g_free(xml);
}

/* === MAIN === */

int main(int argc, char *argv[])
{
    mztest_parse_args(argc, argv);
    mztest_init();

    UNITY_BEGIN();

    /* smoke */
    RUN_TEST(test_xml_writer_create);
    RUN_TEST(test_xml_reader_create);
    RUN_TEST(test_xml_reader_invalid);

    /* unit */
    RUN_TEST(test_xml_roundtrip_string);
    RUN_TEST(test_xml_roundtrip_int);
    RUN_TEST(test_xml_roundtrip_uint);
    RUN_TEST(test_xml_roundtrip_hex8);
    RUN_TEST(test_xml_roundtrip_hex16);
    RUN_TEST(test_xml_roundtrip_hex32);
    RUN_TEST(test_xml_roundtrip_uint64);
    RUN_TEST(test_xml_roundtrip_bool);
    RUN_TEST(test_xml_read_missing_element);
    RUN_TEST(test_xml_enter_missing_element);
    RUN_TEST(test_xml_nested_elements);
    RUN_TEST(test_xml_element_with_attribute);

    int result = UNITY_END();
    mztest_teardown();
    return result;
}
