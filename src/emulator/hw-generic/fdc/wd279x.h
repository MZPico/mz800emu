/**
 * @file wd279x.h
 * @brief WD279x chip vrstva (true-bus).
 *
 * Tato hlavička deklaruje veřejné API kostry WD279x čipu.
 *
 * ## Vrstvy
 *
 * Implementace FDC je rozdělena do čtyř logických vrstev:
 *  1. Sharp BUS (porty 0xD8-0xDF + HD Patch obvod EINT) - fdc.c
 *  2. Translation (inverze D7..D0 mezi BUS a chipem) - fdc.c
 *  3. Chip (WD279x-class) - tento soubor (wd279x.c)
 *  4. Drives (mechaniky 0..3, DSK obrazy) - fdc.c
 *
 * ## True-bus konvence
 *
 * Vrstva chip pracuje výhradně s NEINVERTOVANÝMI hodnotami z datasheetu.
 * Veškerou datovou inverzi (Sharp HW invertuje D0..D7) provádí vrstva
 * BUS/Translation v fdc.c. Tj. registry `regSTATUS`, `regTRACK`,
 * `regSECTOR`, `regDATA` drží hodnoty tak, jak je interně vidí WD279x
 * čip podle datasheetu. Hodnoty Command jsou např. 0x00..0x0F pro
 * RESTORE, 0x10..0x1F pro SEEK, ne jejich invertované verze.
 *
 * Historicky existovala paralelní `_old_` implementace, která pracovala
 * uvnitř s invertovanými hodnotami Command. Po stabilizaci NEW byla OLD
 * odstraněna - inverze se nyní jednotně provádí v BUS vrstvě.
 *
 * License: GPLv3.
 */

#ifndef WD279X_H
#define WD279X_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

    /* Forward deklarace - st_FDDrive je definovaná v fdc.h. */
    struct st_FDDrive;

    /**
     * @brief Velikost interního I/O bufferu chipu.
     *
     * 512B = velikost HD sektoru. Postačí pro SD (256B) a HD (512B) sektory
     * standardních MZ-800 formátů. 1024B sektory nejsou pro MZ-800
     * běžné - pro ně by buffer nestačil.
     */
#define WD279X_BUFFER_SIZE 0x200

    /** @brief Počet mechanik adresovaných chipem (= shoda s fdc.h FDC_NUM_DRIVES). */
#define WD279X_NUM_DRIVES 4

    /**
     * @brief Dekódovaný typ WD279x command opcode.
     *
     * Slouží pro debugger UI a hwlog FDC_COMMAND_ISSUED událost - aby
     * uživatel viděl "Read Sector" / "Restore" místo raw 0x80 / 0x00.
     *
     * Hodnoty MUSÍ zůstat stabilní (= součást debugger persistence formátu
     * v hwlog/eventlog stream). Nepřečíslovat.
     *
     * Mapování raw command byte (top 4 bity) -> typ:
     *  - 0x00..0x0F -> RESTORE (Type I)
     *  - 0x10..0x1F -> SEEK (Type I)
     *  - 0x20..0x3F -> STEP (Type I), bit 4 = update flag
     *  - 0x40..0x5F -> STEP_IN (Type I), bit 4 = update flag
     *  - 0x60..0x7F -> STEP_OUT (Type I), bit 4 = update flag
     *  - 0x80..0x9F -> READ_SECTOR (Type II), bit 4 = multi-block flag
     *  - 0xA0..0xBF -> WRITE_SECTOR (Type II), bit 4 = multi-block flag
     *  - 0xC0..0xCF -> READ_ADDRESS (Type III)
     *  - 0xD0..0xDF -> FORCE_INTERRUPT (Type IV)
     *  - 0xE0..0xEF -> READ_TRACK (Type III)
     *  - 0xF0..0xFF -> WRITE_TRACK (Type III)
     */
    typedef enum en_WD279X_COMMAND_TYPE
    {
        WD279X_CMD_RESTORE         = 0x00, /**< 0x0X - Type I Restore. */
        WD279X_CMD_SEEK            = 0x01, /**< 0x1X - Type I Seek. */
        WD279X_CMD_STEP            = 0x02, /**< 0x2X bez bit 4 - Step bez update. */
        WD279X_CMD_STEP_UPDATE     = 0x03, /**< 0x3X s bit 4 - Step s update Track reg. */
        WD279X_CMD_STEP_IN         = 0x04, /**< 0x4X bez bit 4 - Step In. */
        WD279X_CMD_STEP_IN_UPDATE  = 0x05, /**< 0x5X s bit 4 - Step In s update. */
        WD279X_CMD_STEP_OUT        = 0x06, /**< 0x6X bez bit 4 - Step Out. */
        WD279X_CMD_STEP_OUT_UPDATE = 0x07, /**< 0x7X s bit 4 - Step Out s update. */
        WD279X_CMD_READ_SECTOR     = 0x08, /**< 0x8X / 0x9X - Read Sector. */
        WD279X_CMD_WRITE_SECTOR    = 0x09, /**< 0xAX / 0xBX - Write Sector. */
        WD279X_CMD_READ_ADDRESS    = 0x0A, /**< 0xCX - Read Address. */
        WD279X_CMD_READ_TRACK      = 0x0B, /**< 0xEX - Read Track. */
        WD279X_CMD_WRITE_TRACK     = 0x0C, /**< 0xFX - Write Track. */
        WD279X_CMD_FORCE_INTERRUPT = 0x0D, /**< 0xDX - Force Interrupt (Type IV). */
        WD279X_CMD_UNKNOWN         = 0xFF, /**< Mimo definované opcode rozsahy. */
    } en_WD279X_COMMAND_TYPE;

    /**
     * @brief Dekóduje raw WD279x command byte na typ příkazu.
     *
     * Mapování je side-effect free a thread-safe. Side flag, multi-block flag
     * a update flag se z command bytu NErekonstruují - vrací jen základní typ
     * (volající si může dodatečné bity dohledat v raw cmd byte).
     *
     * @param cmd  Raw command byte (true-bus hodnota, bez Sharp inverze).
     * @return     Odpovídající @ref en_WD279X_COMMAND_TYPE hodnota.
     *             @ref WD279X_CMD_UNKNOWN se nikdy nevrátí (pokrytí je úplné).
     */
    en_WD279X_COMMAND_TYPE wd279x_decode_command_type ( uint8_t cmd );

    /**
     * @brief Vrátí lidsky čitelné anglické jméno @ref en_WD279X_COMMAND_TYPE.
     *
     * Vrací statický string (= ne thread-locked, ne kopírovat). Vhodné pro
     * debugger UI labels a hwlog detail decoder. Pro neznámé hodnoty vrátí
     * @c "Unknown".
     *
     * @param t  Typ příkazu.
     * @return   Statický pointer na NUL-terminated ASCII string.
     */
    const char *wd279x_command_type_name ( en_WD279X_COMMAND_TYPE t );

    /**
     * @brief Status mode - typ posledně přijatého příkazu.
     *
     * Status registr má command-specific sémantiku bitů. Mode se mění
     * při příjmu Command byte podle high nibble. Po Force Interrupt
     * (Type IV) přechází zpět na Type I.
     */
    typedef enum en_WD279X_STATUS_MODE
    {
        WD279X_STATUS_MODE_TYPE_I = 0, /**< Type I (Restore/Seek/Step). */
        WD279X_STATUS_MODE_TYPE_II_III /**< Type II / III (R/W sector, R/W track, R/A). */
    } en_WD279X_STATUS_MODE;

    /**
     * @brief Stav emulovaného WD279x čipu.
     *
     * Operuje v "true-bus" hodnotách (BEZ Sharp inverze). Inverzi provádí
     * volající vrstva (fdc.c BUS/Translation).
     *
     * @invariant `buffer_pos <= WD279X_BUFFER_SIZE`.
     * @invariant `data_counter <= sizeof(buffer)`.
     */
    typedef struct st_WD279X
    {
        uint8_t regSTATUS;  /**< Status registr (true-bus). 0x80 = NOT READY. */
        uint8_t regCOMMAND; /**< Naposledy přijatý příkaz (true-bus, datasheet hodnota). */
        uint8_t regTRACK;   /**< Track registr (true-bus). */
        uint8_t regSECTOR;  /**< Sector registr (true-bus). */
        uint8_t regDATA;    /**< Data registr (true-bus). */

        uint8_t MOTOR;   /**< MOTOR/DRIVE select port (offset 4, 0xDCh). */
        uint8_t SIDE;    /**< SIDE port (offset 5, 0xDDh). */
        uint8_t DENSITY; /**< DENSITY port (offset 6, 0xDEh). */
        uint8_t EINT;    /**< HD Patch EINT port (offset 7, 0xDFh). */

        uint8_t buffer[WD279X_BUFFER_SIZE]; /**< Interní I/O buffer pro sektor. */
        uint16_t buffer_pos;                    /**< Aktuální pozice v bufferu. */
        uint16_t data_counter;                  /**< Zbývajících bajtů pro R/W transfer (0 = idle). */
        uint16_t current_sector_size;           /**< Velikost aktuálně zpracovávaného sektoru v bajtech. */

        en_WD279X_STATUS_MODE status_mode; /**< Sémantika regSTATUS bitů (per command type). */

        uint8_t multiblock_rw;        /**< 1 = m flag z posledního Type II R/W příkazu. */
        int8_t direction_latch;       /**< +1 = poslední pohyb dovnitř, -1 = ven. Přežívá Force Interrupt. */
        uint8_t intrq_active;         /**< 1 = sticky INTRQ aktivní (od FORCE INT 0xD8 a po dokončení příkazu). */
        uint8_t reading_status_counter; /**< Počet po sobě jdoucích čtení STATUS bez čtení DATA. */
        uint8_t pending_busy_status;  /**< 1 = další čtení STATUS vrátí BUSY=1 (one-shot po Type I příkazu, symetrie s _old_ STATUS_SCRIPT case 1). */
        uint8_t pending_drq;          /**< 1 = další čtení STATUS v Type II/III vrátí BUSY only (bez DRQ); až další čtení vrátí BUSY+DRQ. Symetrie s _old_ STATUS_SCRIPT case 2 (multi-block sector transition). CP/M 4.1 race condition se spoléhá na tuto prodlevu. */
        uint8_t pending_rnf_end;      /**< 1 = multi-block R/W narazil na konec stopy (další sektor v pořadí neexistuje). První čtení STATUS vrátí BUSY+RNF (0x11), regSTATUS se pak vyčistí na 0x00. Symetrie s _old_ STATUS_SCRIPT case 4 ("1x BUSY+RNF, next 0x00"). MZ-800 disk BASIC čte adresář multi-blokem a spoléhá na to, že RNF na konci stopy je přechodný (BUSY=1, hned poté čisto), NE trvalý "command complete + RNF" - jinak hlásí FD1:Unformat error. */
        uint8_t positioned_sector;    /**< "Drive head position" - sektor ID na který je hlava aktuálně nastavená. Ekvivalent _old_ drive.SECTOR. Reset na 0 při track NEBO side change, update na READ SECTOR. Použito v READ ADDRESS pro buffer[1] - matchuje _old_ chování. */
        uint8_t positioned_track;     /**< Posledný track na který se hlava reálně přesunula. Ekvivalent _old_ drive.TRACK. */
        uint8_t positioned_side;      /**< Posledná strana - drive.SIDE equivalent. */
        uint8_t waitForInt;           /**< Throttle čítač pro HD Patch INT generaci (port 0xDFh EINT režim). INT se vystaví až po N po sobě jdoucích check_interrupt voláních. Symetrie s _old_ FDC->waitForInt. */

        /* WRITE TRACK (formátování stopy) - vícestupňový state machine
         * řízený příchodem markerů (IAM/IDAM/DATA_AM) v byte streamu z CPU.
         * Symetrie s _old_ write_track_stage / write_track_counter. */
        uint8_t write_track_stage;    /**< Aktuální fáze formátování (0..5). 0 = idle / waiting for IAM. */
        uint16_t write_track_counter; /**< Počet bajtů přijatých ve stávající fázi - timeout / sektor data progress. */

        /* READ TRACK (raw track stream) - syntetizovaný byte stream
         * generovaný on-the-fly při čtení DATA portu během 0xE0..0xEF
         * příkazu. Pro každý DATA read se vypočítá byte ze struktury
         * Track-Info nacachované při setup. */
        uint8_t rt_sectors;             /**< Počet sektorů na aktuální stopě (z tinfo). */
        uint16_t rt_sector_bytes;       /**< Velikost sektoru v bajtech (decoded z ssize). */
        uint8_t rt_ssize_code;          /**< WD279x ssize code (datasheet 0..3) - byte 4 ID bloku. */
        int8_t rt_cached_sec_idx;       /**< Index sektoru právě v chip->buffer (-1 = žádný). */
        uint8_t rt_sinfo[29];           /**< Sector ID list (DSK_MAX_SECTORS = 29). */

        /* Drives accessor - pointer na pole 4 mechanik (vlastní fdc.c).
         * Nastavuje fdc_init() po init chipu. */
        struct st_FDDrive *drives; /**< Ukazatel na pole drives[FDC_NUM_DRIVES]. */

        /* HD Patch konfigurace vlastnící instance st_FDC (= &st_FDC.hd_patch).
         * Chip je instance-agnostic, ale HD Patch INT logika (port 0xDFh
         * EINT) potřebuje znát, zda je HD Patch obvod osazen. Ukazatel
         * nastavuje fdc_init() po init chipu (jako drives). NULL = HD Patch
         * absent (EINT INT nikdy nefire). Není serializován ve snapshotu -
         * obnovuje se při init. */
        const int *hd_patch_ref; /**< &st_FDC.hd_patch vlastnící instance, nebo NULL. */

        /* TODO (fáze C): timing counters (step rate, spin-up, settling). */
        /* TODO (fáze C): live INDEX pulse timer (oscilace S1). */
    } st_WD279X;

    /**
     * @brief Inicializace chipu - poweron stav.
     *
     * Vynuluje strukturu a nastaví resetový stav (`regSTATUS = 0x80`,
     * Type I mode, žádný drive accessor). Drive accessor je nutné
     * nastavit volajícím přes wd279x_attach_drives().
     *
     * @param chip ukazatel na strukturu chipu (musí být platný).
     */
    extern void wd279x_init(st_WD279X *chip);

    /**
     * @brief Reset chipu (HW reset emulovaného systému).
     *
     * Vynuluje registry, nastaví status mode na Type I, resetuje
     * data_counter a buffer_pos. Nezavírá DSK obrazy (to je v doméně
     * vrstvy Drives - fdc.c).
     *
     * Pozn.: direction_latch se resetuje na +1 (step-in, default po
     * power-on; po automatickém RESTORE se nastaví na -1).
     *
     * @param chip ukazatel na strukturu chipu.
     */
    extern void wd279x_reset(st_WD279X *chip);

    /**
     * @brief Napoj chip na pole mechanik.
     *
     * Chip potřebuje přístup k mounted DSK obrazům pro READ/WRITE
     * SECTOR operace. Volá se po wd279x_init() (typicky z
     * fdc_init()).
     *
     * @param chip ukazatel na strukturu chipu.
     * @param drives ukazatel na první prvek pole st_FDDrive
     *               (= `&g_fdc[i].drive[0]`).
     */
    extern void wd279x_attach_drives(st_WD279X *chip, struct st_FDDrive *drives);

    /**
     * @brief Čtení registru chipu (true-bus hodnota).
     *
     * Operuje s NEINVERTOVANÝMI hodnotami - vrátí přesně to, co by
     * WD279x čip vystavil na svých D7..D0 pinech.
     *
     * @param chip ukazatel na strukturu chipu.
     * @param i_addroffset offset registru (0..7) - viz mapování v
     *                     fdc.c (FDCPORT_*).
     * @param io_data výstupní byte (true-bus hodnota).
     * @return 0 = OK.
     *
     * @note Vedlejší efekty:
     *  - Čtení STATUS resetuje INTRQ (pokud nebyl 0xD8 Force Interrupt).
     *  - Čtení DATA v běžícím Type II resetuje DRQ + dekrementuje
     *    data_counter, eventuálně přesouvá na další sektor (multiblock).
     */
    extern int wd279x_read_byte(st_WD279X *chip, int i_addroffset, uint8_t *io_data);

    /**
     * @brief Zápis do registru chipu (true-bus hodnota).
     *
     * Operuje s NEINVERTOVANÝMI hodnotami.
     *
     * @param chip ukazatel na strukturu chipu.
     * @param i_addroffset offset registru (0..7).
     * @param io_data ukazatel na vstupní byte (true-bus hodnota).
     * @return 0 = OK.
     *
     * @note Vedlejší efekty:
     *  - Zápis do COMMAND resetuje INTRQ a dispatchne příkaz.
     *  - Zápis do DATA v běžícím WRITE SECTOR ukládá do bufferu a po
     *    posledním bajtu provede DSK zápis.
     */
    extern int wd279x_write_byte(st_WD279X *chip, int i_addroffset, uint8_t *io_data);

    /**
     * @brief Stav /INT signálu chipu.
     *
     * @param chip ukazatel na strukturu chipu.
     * @return 0 = INT není aktivní, nenulová = INT aktivní.
     *
     * @note Fáze B: pouze sticky INTRQ z FORCE INT 0xD8 (pokud
     *       EINT enabled). DRQ-based INT v HD patchi neimplementován.
     */
    extern int wd279x_get_interrupt_state(st_WD279X *chip);

    /**
     * @brief Side-effect free čtení status registru pro debug/UI mirror.
     *
     * Vrací status jak by ho UI viděla, ale **NEVYVOLÁVÁ** žádný z
     * side-effectů reálné `wd279x_read_byte()` cesty - tj. nemodifikuje
     * `pending_busy_status`, `pending_drq`, `reading_status_counter`,
     * `regSTATUS` ani `intrq_active`.
     *
     * Pro Type I rebuilduje status přes interní `build_type1_status()`
     * (= aplikuje live NOT_READY z drive_is_ready()), pro Type II/III
     * vrací surový `chip->regSTATUS`.
     *
     * Pending one-shot bity (BUSY z `pending_busy_status` a DRQ z
     * `pending_drq` v Type II/III) NEjsou v mirror započítány - mirror
     * ukazuje "klidový" stav, ne výsledek příští IORQ READ side-effect
     * cesty.
     *
     * @param chip ukazatel na strukturu chipu (NESMÍ být NULL).
     * @return surový status byte (true-bus, bez Sharp inverze).
     *
     * @note Volání z UI vlákna v paralelním běhu emu vlákna je race-tolerant
     *       (single-byte snapshot, x86 atomic). Stejný pattern jako
     *       `read_gdg_status()` v io_catalog.c.
     */
    extern uint8_t wd279x_mirror_status_get(st_WD279X *chip);

    /**
     * @brief Side-effect free čtení track registru pro debug/UI mirror.
     *
     * Vrací `chip->regTRACK` bez vyvolání `waitForInt = 0` resetu, který
     * by reálná `wd279x_read_byte()` cesta provedla.
     *
     * @param chip ukazatel na strukturu chipu (NESMÍ být NULL).
     * @return aktuální track registr (true-bus).
     */
    extern uint8_t wd279x_mirror_track_get(st_WD279X *chip);

    /**
     * @brief Side-effect free čtení sector registru pro debug/UI mirror.
     *
     * Vrací `chip->regSECTOR`. Reálná IORQ read cesta sector registru
     * nemá side-effecty, mirror je tedy 1:1.
     *
     * @param chip ukazatel na strukturu chipu (NESMÍ být NULL).
     * @return aktuální sector registr (true-bus).
     */
    extern uint8_t wd279x_mirror_sector_get(st_WD279X *chip);

#ifdef __cplusplus
}
#endif

#endif /* WD279X_H */
