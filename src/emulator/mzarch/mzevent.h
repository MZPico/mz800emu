#ifndef MZEVENT_H
#define MZEVENT_H

typedef enum en_MZEVENT
{
    //        MZEVENT_GDG_STS_HSYNC_END,
    MZEVENT_GDG_HBLN_END,
    MZEVENT_GDG_HBLN_START,
    MZEVENT_GDG_STS_VSYNC_END,
    MZEVENT_GDG_STS_VSYNC_START,
    MZEVENT_GDG_AFTER_LAST_SCREEN_PIXEL,
    //        MZEVENT_GDG_STS_HSYNC_START,
    MZEVENT_GDG_AFTER_LAST_VISIBLE_PIXEL,
    MZEVENT_GDG_REAL_HSYNC_START,
    MZEVENT_GDG_SCREEN_ROW_END,

    // jine, nez GDG eventy
    MZEVENT_NO_GDG, /* pouze hranicni hodnota - neni skutecny event */

    MZEVENT_PIOZ80,

#ifdef MZ800EMU_CFG_CLK1M1_FAST
    MZEVENT_CTC0,
#endif

    MZEVENT_CUSTOM_SPEED_SYNCHRONISATION,
    MZEVENT_BREAK, /* pouze hranicni hodnota - neni skutecny event */

    // V prubehu zpracovani instrukce vznikl MZ800 interrupt.
    // Tento event slouzi k tomu, aby jsme vybehli z instrukcni smycky a pokusili se jej prevzit.
    MZEVENT_BREAK_MZARCH_INTERRUPT,

    MZEVENT_BREAK_EMULATION_PAUSED,
} en_MZEVENT;

typedef struct st_EMUEVENT
{
    en_MZEVENT event_name;
    unsigned ticks;
} st_EMUEVENT;

#endif /* MZEVENT_H */
