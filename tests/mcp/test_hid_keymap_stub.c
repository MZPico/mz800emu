/*
 * File:   test_hid_keymap_stub.c
 *
 * Test stub pro hid_keymap.c v MCP standalone testovacím buildu
 * (`MZ800EMU_MCP_TEST_BUILD`). Poskytuje:
 *
 *   - `pio8255_autotype_get_matrix(c, *ret, *ret_shift)` - minimální
 *     Sharp ASCII mapping pro klíčové testovací znaky (písmena, číslice,
 *     SPACE, RETURN, ...). Vrátí col/row mask kompatibilní s formátem
 *     reálné implementace v hw-generic/pio8255/pio8255.c.
 *   - `hid_keymap_press / release / release_all / joystick_set /
 *     joystick_clear` - tyto symboly v hid_keymap.c jsou pod
 *     `#ifdef MZ800EMU_MCP_TEST_BUILD` přesunuté do tohoto stubu (=
 *     test_build vidí hlavičku, ale linker bere implementaci odtud).
 *     Stub zachytí volání do g_stub_state polí pro inspekci v testech.
 *
 * Licence: GPLv3.
 */

#include "test_dispatch_dbgapi_stub.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>


/* ============================================================================
 * Sharp ASCII mapping stub
 *
 * Minimální výřez z reálné tabulky pio8255_autotype_get_matrix. Pokrývá
 * znaky používané v V1.C.1 testech (RUN, A-Z, 0-9, '\r', ' ').
 *
 * Formát ret = 0xff & ~(1 << row) - identicky s reálnou implementací.
 * ============================================================================ */

typedef struct st_HID_TESTKEY {
    char c;
    int  col;
    int  row;
    int  shift;
} st_HID_TESTKEY;

static const st_HID_TESTKEY g_test_keys[] = {
    /* col0 / RETURN */
    { '\r', 0, 0, 0 },
    { '\n', 0, 0, 0 },
    { '\t', 0, 3, 0 },

    /* col1 - Y Z */
    { 'Y', 1, 7, 0 }, { 'y', 1, 7, 1 },
    { 'Z', 1, 6, 0 }, { 'z', 1, 6, 1 },

    /* col2 - Q R S T U V W X */
    { 'Q', 2, 7, 0 }, { 'R', 2, 6, 0 }, { 'S', 2, 5, 0 }, { 'T', 2, 4, 0 },
    { 'U', 2, 3, 0 }, { 'V', 2, 2, 0 }, { 'W', 2, 1, 0 }, { 'X', 2, 0, 0 },

    /* col3 - I J K L M N O P */
    { 'I', 3, 7, 0 }, { 'J', 3, 6, 0 }, { 'K', 3, 5, 0 }, { 'L', 3, 4, 0 },
    { 'M', 3, 3, 0 }, { 'N', 3, 2, 0 }, { 'O', 3, 1, 0 }, { 'P', 3, 0, 0 },

    /* col4 - A B C D E F G H */
    { 'A', 4, 7, 0 }, { 'B', 4, 6, 0 }, { 'C', 4, 5, 0 }, { 'D', 4, 4, 0 },
    { 'E', 4, 3, 0 }, { 'F', 4, 2, 0 }, { 'G', 4, 1, 0 }, { 'H', 4, 0, 0 },

    /* col5 - 1..8 */
    { '1', 5, 7, 0 }, { '2', 5, 6, 0 }, { '3', 5, 5, 0 }, { '4', 5, 4, 0 },
    { '5', 5, 3, 0 }, { '6', 5, 2, 0 }, { '7', 5, 1, 0 }, { '8', 5, 0, 0 },

    /* col6 - SPACE 0 9 */
    { ' ', 6, 4, 0 },
    { '0', 6, 3, 0 },
    { '9', 6, 2, 0 },

    /* col7 - / */
    { '/', 7, 0, 0 },

    { 0, 0, 0, 0 }
};


/**
 * @brief Minimální stub Sharp ASCII -> col/row mapping pro test build.
 *
 * Vrátí col při úspěchu, -1 jinak. `ret` naplní invert-bit maskou
 * (= 0xff & ~(1 << row)). Identický kontrakt s reálnou
 * `pio8255_autotype_get_matrix` z hw-generic/pio8255/pio8255.c.
 */
int pio8255_autotype_get_matrix(char c, uint8_t *ret, bool *ret_shift) {
    if (!ret || !ret_shift) return -1;
    for (int i = 0; g_test_keys[i].c != 0; i++) {
        if (g_test_keys[i].c == c) {
            *ret = (uint8_t)(0xff & ~(1 << g_test_keys[i].row));
            *ret_shift = g_test_keys[i].shift ? true : false;
            return g_test_keys[i].col;
        }
    }
    return -1;
}


/* ============================================================================
 * Press / release / joystick stub
 *
 * V test buildu jsou tyto symboly z hid_keymap.c vynechány
 * (#ifdef MZ800EMU_MCP_TEST_BUILD). Tento stub poskytne implementaci,
 * která jen zaznamenává parametry do g_stub_state.hid_* polí.
 * ============================================================================ */

void hid_keymap_press(int col, int bit, bool also_shift) {
    g_stub_state.hid_press_calls++;
    g_stub_state.hid_last_col = col;
    g_stub_state.hid_last_bit = bit;
    g_stub_state.hid_last_shift = also_shift ? 1 : 0;
}


void hid_keymap_release(int col, int bit, bool also_shift) {
    g_stub_state.hid_release_calls++;
    g_stub_state.hid_last_col = col;
    g_stub_state.hid_last_bit = bit;
    g_stub_state.hid_last_shift = also_shift ? 1 : 0;
}


void hid_keymap_release_all(void) {
    g_stub_state.hid_release_all_calls++;
}


bool hid_keymap_joystick_set(int port, uint8_t mcp_mask) {
    g_stub_state.hid_joy_set_calls++;
    g_stub_state.hid_joy_last_port = port;
    g_stub_state.hid_joy_last_mask = mcp_mask;
    if (port < 0 || port > 1) return false;
    return true;
}


bool hid_keymap_joystick_clear(int port) {
    g_stub_state.hid_joy_clear_calls++;
    g_stub_state.hid_joy_last_port = port;
    if (port < 0 || port > 1) return false;
    return true;
}
