/**
 * @file wd279x_internal.h
 * @brief Privátní sdílené API mezi překládacími jednotkami implementace
 *        WD279x čipu.
 *
 * Tento header NENÍ veřejné API - jeho includují pouze .c soubory tvořící
 * implementaci chipu (wd279x.c, wd279x_write_track.c,
 * wd279x_read_track.c). Veřejné API je v wd279x.h.
 *
 * Důvod existence: refactor 2000+ řádkového wd279x.c na menší
 * překládací jednotky (chip core + WRITE TRACK + READ TRACK). Sdílené
 * konstanty (status bity, port offsety) a helper funkce musí být dostupné
 * napříč soubory bez `static` modifikátoru.
 *
 * License: GPLv3 - viz wd279x.h.
 */

#ifndef WD279X_INTERNAL_H
#define WD279X_INTERNAL_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include "wd279x.h"

    /* ====================================================================
     * Port-offset registrů (= port adresa & 0x07)
     * ==================================================================== */

    typedef enum en_FDCPORT_OFFSET
    {
        FDCPORT_CMDSTS = 0,  /**< 0xD8h - Status R / Command W */
        FDCPORT_TRACK = 1,   /**< 0xD9h - Track */
        FDCPORT_SECTOR = 2,  /**< 0xDAh - Sector */
        FDCPORT_DATA = 3,    /**< 0xDBh - Data */
        FDCPORT_MOTOR = 4,   /**< 0xDCh - Motor / drive select */
        FDCPORT_SIDE = 5,    /**< 0xDDh - Side */
        FDCPORT_DENSITY = 6, /**< 0xDEh - Density */
        FDCPORT_EINT = 7,    /**< 0xDFh - HD Patch EINT */
    } en_FDCPORT_OFFSET;

    /* ====================================================================
     * Status bity (true-bus hodnoty z datasheetu)
     * ==================================================================== */

    /* Společné bity. */
#define WDST_BUSY         0x01
#define WDST_NOT_READY    0x80

    /* Type I specifické. */
#define WDST_T1_INDEX     0x02
#define WDST_T1_TRACK0    0x04
#define WDST_T1_CRC_ERR   0x08
#define WDST_T1_SEEK_ERR  0x10
#define WDST_T1_HEAD_LOAD 0x20
#define WDST_T1_WP        0x40

    /* Type II/III specifické. */
#define WDST_T2_DRQ       0x02
#define WDST_T2_LOST_DATA 0x04
#define WDST_T2_CRC_ERR   0x08
#define WDST_T2_RNF       0x10
#define WDST_T2_RTYPE     0x20  /* read: 1 = deleted FB; write: WRITE_FAULT */
#define WDST_T2_WP        0x40  /* jen WRITE příkazy */

    /* ====================================================================
     * Sdílené helper funkce (definované v wd279x.c, volané ze split souborů)
     * ==================================================================== */

    /**
     * @brief Vrátí ukazatel na aktuálně vybranou mechaniku.
     *
     * @param chip ukazatel na chip s nastaveným chip->drives.
     * @return ukazatel na st_FDDrive (může být NULL pokud drives není
     *         nastaveno).
     */
    extern struct st_FDDrive *current_drive(st_WD279X *chip);

    /**
     * @brief Test zda je aktuálně vybraná mechanika ready.
     *
     * Ready = mounted && handler_valid && geometry_valid.
     *
     * @param chip ukazatel na chip.
     * @return 1 = ready, 0 = not ready.
     */
    extern int drive_is_ready(st_WD279X *chip);

    /**
     * @brief Vypočítá absolutní stopu pro side-interleaved Extended DSK.
     *
     * abstrack = track * sides + side. Validuje rozsahy proti
     * drv->geometry.
     *
     * @param drv mechanika (musí být ready).
     * @param track logická stopa (0..tracks-1).
     * @param side strana (0 nebo 1).
     * @return abstrack (0..159 typically), nebo 0xFF při chybě.
     */
    extern uint8_t compute_abstrack(struct st_FDDrive *drv, uint8_t track, uint8_t side);

    /**
     * @brief Synchronizuje "drive head position" s registry chipu.
     *
     * Reset positioned_sector pokud se track NEBO side změnily. Volá se
     * před Type II/III příkazy.
     *
     * @param chip ukazatel na chip.
     * @param drv mechanika (musí být ready).
     * @return 1 = OK (track existuje), 0 = mimo geometrii.
     */
    extern int set_track_position(st_WD279X *chip, struct st_FDDrive *drv);

    /* ====================================================================
     * Type III WRITE TRACK API (definováno v wd279x_write_track.c,
     * voláno z wd279x.c dispatch / DATA write handler)
     * ==================================================================== */

    /**
     * @brief Inicializuje stav chipu po příjmu WRITE TRACK příkazu (0xF0..0xFF).
     */
    extern void do_write_track_setup(st_WD279X *chip, uint8_t command);

    /**
     * @brief Zpracuje jeden byte data z CPU během WRITE TRACK.
     *
     * Volá se z wd279x_write_byte() DATA register handler.
     */
    extern void do_write_track_byte(st_WD279X *chip, uint8_t io_data);

    /* ====================================================================
     * Type III READ TRACK API (definováno v wd279x_read_track.c,
     * voláno z wd279x.c dispatch / DATA read handler)
     * ==================================================================== */

    /**
     * @brief Inicializuje READ TRACK streaming state (0xE0..0xEF).
     */
    extern void do_read_track_setup(st_WD279X *chip, uint8_t command);

    /**
     * @brief Vrátí další byte syntetického READ TRACK streamu.
     *
     * Volá se z wd279x_read_byte() DATA register handler.
     */
    extern uint8_t rt_synth_byte(st_WD279X *chip);

#ifdef __cplusplus
}
#endif

#endif /* WD279X_INTERNAL_H */
