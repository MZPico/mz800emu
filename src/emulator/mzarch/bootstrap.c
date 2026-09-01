#include "main.h"

#include "mzarch/mzarch_config.h"
#include "mzarch/bootstrap.h"
#include "libs/mzf/mzf_tools.h"
#include "hw-generic/gdg/gdg.h"
#include "hw-generic/ctc8253/ctc8253.h"
#include "hw-generic/pio8255/pio8255.h"
#include "hw-generic/pioz80/pioz80.h"
#include "hw-generic/memory/memory.h"
#include "hw-generic/cmt/cmthack.h"
#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED
#include "debugger/debugger.h"
#else
#include "baseui/baseui.h"
#include "emulator.h"
#endif
#include "libs/cpu-z80/z80.h"


static void mzarch_bootstrap_init(void)
{
    // 8255 init (master_port)
    pio8255_write(3, 0x8a);
    pio8255_write(3, 0x07);
    pio8255_write(3, 0x05);
    pio8255_write(3, 0x01); /* PC0 = CTC0 audio unmasked (MZ-800/MZ-1500 gate the speaker with it; the ROM sets it at boot) */

    // CTC init
    ctc8253_write_byte(3, 0x36); // channel 0 = sound: mode 3 (square wave), LSB+MSB — the monitor does this at boot
    ctc8253_write_byte(3, 0x74);
    ctc8253_write_byte(3, 0xb0);
    ctc8253_write_byte(2, 0xc0);
    ctc8253_write_byte(2, 0xa8);
    ctc8253_write_byte(1, 0xa0);
    ctc8253_write_byte(1, 0x00);
    ctc8253_write_byte(3, 0x80);
    ctc8253_write_byte(1, 0xfb);
    ctc8253_write_byte(1, 0x3c);

#if HAVE_PIOZ80
    // PIO init
    pioz80_write_byte(0, 0x00);
    pioz80_write_byte(0, 0xcf);
    pioz80_write_byte(0, 0x3f);
    pioz80_write_byte(0, 0x07);
    pioz80_write_byte(1, 0x00);
    pioz80_write_byte(1, 0xcf);
    pioz80_write_byte(1, 0x00);
    pioz80_write_byte(1, 0x07);
#endif

    // Zavolame platform-specific bootstrap init, ktery muze nastavit mapovani pameti a podobne
    mzarch_platform_bootstrap_init();

    /* Monitor work area as the ROM leaves it before jumping to a loaded
     * program (captured on the ROM path; only title-independent bytes):
     *   1038h: JP 038Dh  - interrupt vector used by the ROM's RST 38h handler
     *   119Dh..11A2h: 01 04 00 02 EC 04 - display/sound variables
     * Written before the MZF body so a program loading over this range wins,
     * exactly as on the real machine. */
    static const uint8_t int_vector[] = { 0xc3, 0x8d, 0x03 };
    static const uint8_t disp_vars[]  = { 0x01, 0x04, 0x00, 0x02, 0xec, 0x04 };
    for (size_t i = 0; i < sizeof(int_vector); i++) memory_write_byte((uint16_t)(0x1038 + i), int_vector[i]);
    for (size_t i = 0; i < sizeof(disp_vars); i++)  memory_write_byte((uint16_t)(0x119d + i), disp_vars[i]);
}

void mzarch_bootstrap_run_mzf(const char *filename)
{
    printf("Bootstrapping...\n");

    mzarch_bootstrap_init();

    z80_set_reg(g_mzarch_main.cpu, Z80_REG_HL, 0x10f0);
    cmthack_load_mzf_filename(filename);

    st_MZF_HEADER mzf_header;

#ifdef MZ800EMU_CFG_DEBUGGER_ENABLED
    for (size_t i = 0; i < sizeof(st_MZF_HEADER); i++)
    {
        uint8_t *p = (uint8_t *)&mzf_header + i;
        *p = debugger_memory_read_byte(0x10f0 + i);
    };
#else
    FILE *f = baseui_tools_file_open(filename, "rb");
    if (!f)
    {
        baseui_show_error_message("Cannot open file ''%s'' for reading.", filename);
        emulator_quit(EXIT_FAILURE);
    };
    if (baseui_tools_file_read(&mzf_header, sizeof(st_MZF_HEADER), 1, f) != 1)
    {
        baseui_show_error_message("Cannot read header from file ''%s''.", filename);
        emulator_quit(EXIT_FAILURE);
    };
    baseui_tools_file_close(f);
#endif

    /* Post-header platform-specific úpravy mapování paměti. Musí být
     * PŘED cmthack_read_mzf_body() - jinak by tělo MZF zapisovalo přes
     * špatně mapovanou ROM (např. fstrt < 0x1000 by se neuložilo do RAM).
     *
     * Společné chování (= odmapování dolní ROM pro fstrt < 0x1000) +
     * platform-specific (= MZ-800 mode přepnutí na 320x200@4A) viz
     * deklaraci v bootstrap.h. */
    mzarch_platform_bootstrap_post_header(mzf_header.fstrt);

    z80_set_reg(g_mzarch_main.cpu, Z80_REG_HL, mzf_header.fstrt);
    z80_set_reg(g_mzarch_main.cpu, Z80_REG_BC, mzf_header.fsize);
    cmthack_read_mzf_body();
    /* Register state as the monitor ROM's loader leaves it when it jumps to
     * the program (measured on the ROM path: HL=1200h, BC=0100h, DE=0,
     * IX=exec, IY=0, SP=10F0h). */
    z80_set_reg(g_mzarch_main.cpu, Z80_REG_HL, 0x1200);
    z80_set_reg(g_mzarch_main.cpu, Z80_REG_BC, 0x0100);
    z80_set_reg(g_mzarch_main.cpu, Z80_REG_DE, 0x0000);
    z80_set_reg(g_mzarch_main.cpu, Z80_REG_IX, mzf_header.fexec);
    z80_set_reg(g_mzarch_main.cpu, Z80_REG_IY, 0x0000);
    z80_set_reg(g_mzarch_main.cpu, Z80_REG_SP, 0x10f0);
    z80_set_reg(g_mzarch_main.cpu, Z80_REG_PC, mzf_header.fexec);

    /* The monitor ROM hands control to a loaded program with IM 1 and
     * interrupts enabled; programs such as 1Z-016 BASIC install their own
     * vector at 0x0038 but never execute EI themselves, so without this the
     * 8253 interrupt (cursor blink, TIME$, music) never fires. */
    g_mzarch_main.cpu->im = 1;
    g_mzarch_main.cpu->iff1 = 1;
    g_mzarch_main.cpu->iff2 = 1;

    g_print("Bootstrap done.\n");
}
