/**
 * @file   reclife.h
 * @brief  Sdílený recording lifecycle layer (reclife) pro debug subsystémy.
 *
 * Sjednocuje dosud 5x duplikovanou mode-state mašinu trace-suite subsystémů
 * (cputrack/iorqlog/intlog/hwlog/marklog) a poskytuje sdílenou vrstvu pro
 * CDL (mhmap) finalize/save. Subsystém dodá deskriptor @ref st_RECLIFE_DESC
 * (pointery na svůj mode + active flag a callbacky start/stop/reset/save),
 * vrstva reclife rozhodne o přechodu start/stop a o uložení.
 *
 * @section design Návrh
 *
 * Cíl je odstranit tři nezávislé implementace téže lifecycle logiky a dát
 * budoucímu subsystému (např. trace 0017) jediný vzor: vyplnit deskriptor,
 * zavolat @ref reclife_recompute_active / @ref reclife_finalize, hotovo.
 * Žádná čtvrtá kopie switch(mode) ani vlastní finalize gate.
 *
 * @section hotpath Hot-path neovlivněn
 *
 * Per-instrukční gate ve smyčce CPU zůstává plain @c int *_active flag
 * (např. @c TEST_TRACE_CPUTRACK_ACTIVE = @c g_cputrack_active). Deskriptor
 * se čte JEN při @ref reclife_recompute_active (= mode change / debugger
 * toggle), nikdy per-instrukci. Tím se dodržuje pravidlo "emu hot-path
 * nesmí větvit feature checky".
 *
 * @section equiv Byte-ekvivalence s původní logikou
 *
 * @ref reclife_recompute_active je parametrizovaná verze dosavadního
 * @c cputrack_recompute_active (a 4 jeho dvojčat): identický switch
 * OFF/WITH_WINDOW/ALWAYS a identická posloupnost start->set active /
 * stop->clear active. Hodnoty mode enumu (0/1/2) se shodují s
 * @c en_TLOG_MODE i @c en_DEBUGGER_MHMAP_MODE.
 *
 * @author Michal Hucik <hucik@ordoz.com>
 */

#ifndef RECLIFE_H
#define RECLIFE_H

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Režim aktivace recording subsystému.
 *
 * Sdílený mode enum napříč všemi recording subsystémy. Hodnoty se ZÁMĚRNĚ
 * shodují s @c en_TLOG_MODE (tlog_common.h) a @c en_DEBUGGER_MHMAP_MODE
 * (debugger.h), aby byl deskriptor @ref st_RECLIFE_DESC schopen ukazovat
 * na mode pole kteréhokoliv subsystému přes @c int* bez konverze.
 */
typedef enum en_RECLIFE_MODE
{
    RECLIFE_MODE_OFF         = 0, /**< Subsystém vypnutý, žádná režie. */
    RECLIFE_MODE_WITH_WINDOW = 1, /**< Aktivní jen pokud je otevřené debug okno. */
    RECLIFE_MODE_ALWAYS      = 2, /**< Aktivní trvale. */
} en_RECLIFE_MODE;

/**
 * @brief Deskriptor recording subsystému pro sdílenou lifecycle vrstvu.
 *
 * Subsystém vlastní jednu statickou instanci tohoto deskriptoru a předává
 * ji do reclife funkcí. Vrstva reclife přes pointery čte/nastavuje stav
 * subsystému a volá jeho callbacky; sama si nedrží žádný stav.
 *
 * @invariant @c mode_ptr a @c active_ptr musí být platné po celou dobu
 *            existence subsystému (typ. ukazují do globálního configu /
 *            globálního flagu). @c fn_start a @c fn_stop musí být nenulové
 *            pro subsystémy používající @ref reclife_recompute_active.
 *
 * @note @c mode_ptr je @c int* (ne enum*), protože C enum je
 *       int-kompatibilní a tím deskriptor pasuje na @c en_TLOG_MODE i
 *       @c en_DEBUGGER_MHMAP_MODE pole bez přetypování v call-site.
 *
 * @ownership Deskriptor ani jeho pointery reclife nevlastní - jen referuje.
 */
typedef struct st_RECLIFE_DESC
{
    const char *subsys_name;        /**< Identifikátor ("cputrack", "cdl", ...), string literal. */
    int *mode_ptr;                  /**< Ukazatel na mode subsystému (en_RECLIFE_MODE hodnoty). */
    int *active_ptr;                /**< Ukazatel na _active flag subsystému (0/1). */
    int (*fn_start) ( void );       /**< Spustit recording (0 = OK, -1 = fail). NULL u save-only subsystémů. */
    void (*fn_stop) ( void );       /**< Zastavit recording. NULL u save-only subsystémů. */
    void (*fn_reset) ( void );      /**< Volitelný reset stavu (NULL = no-op). */
    int (*fn_save) ( const char *path ); /**< Volitelný save do path (NULL = nepodporuje save(path)). */
} st_RECLIFE_DESC;

/**
 * @brief Přepočítat aktivaci subsystému z mode + debugger stavu.
 *
 * Byte-ekvivalent dosavadního @c cputrack_recompute_active (a dvojčat):
 *  - OFF         -> want = 0
 *  - WITH_WINDOW -> want = debugger_active
 *  - ALWAYS      -> want = 1
 * Při přechodu 0->1 zavolá @c fn_start a při úspěchu (návrat 0) nastaví
 * @c *active_ptr = 1. Při přechodu 1->0 zavolá @c fn_stop a nastaví
 * @c *active_ptr = 0.
 *
 * @param d                Deskriptor subsystému (musí mít fn_start/fn_stop).
 * @param debugger_active  1 = debug okno otevřené (relevantní pro WITH_WINDOW).
 *
 * @pre @c d != NULL, @c d->mode_ptr != NULL, @c d->active_ptr != NULL,
 *      @c d->fn_start != NULL, @c d->fn_stop != NULL.
 * @post @c *d->active_ptr odpovídá požadovanému stavu (pokud start neselhal).
 */
void reclife_recompute_active ( const st_RECLIFE_DESC *d, int debugger_active );

/**
 * @brief Uložit recording do zadané cesty (nebo přes default subsystému).
 *
 * Pokud má deskriptor @c fn_save, deleguje na něj (předá @p path beze změny;
 * subsystém si interpretuje NULL jako "default cesta"). Pokud @c fn_save je
 * NULL, vrátí -1 (subsystém save(path) nepodporuje).
 *
 * @param d     Deskriptor subsystému.
 * @param path  Cílová cesta, nebo NULL pro subsystémem definovaný default.
 *
 * @pre @c d != NULL.
 * @return 0 OK, -1 chyba nebo nepodporováno.
 */
int reclife_save ( const st_RECLIFE_DESC *d, const char *path );

/**
 * @brief Přesměrovat výstup recordingu na cestu @p path (dir + basename).
 *
 * Pomocná funkce pro implementaci @c fn_save streamovaných trace subsystémů
 * (cputrack/iorqlog/intlog/hwlog). Rozparsuje @p path na adresář a basename
 * (přípona se NEodřezává - tlog writer ji použije jako prefix chunk souborů)
 * a nahradí jimi cílový @p *dir_ptr / @p *name_ptr. Stará alokace se uvolní
 * přes @c g_free, nová se alokuje přes @c g_strdup.
 *
 * Funkce sama recording nezastaví ani nerestartuje - to je zodpovědnost
 * volajícího (typ. stop + start kolem tohoto volání), protože tlog writer
 * váže dir/name v okamžiku @c *_start a otevřené chunk soubory si drží.
 *
 * @param dir_ptr   Ukazatel na @c char* pole configu s adresářem (vlastní string).
 * @param name_ptr  Ukazatel na @c char* pole configu s basename (vlastní string).
 * @param path      Cílová cesta. Pokud NULL nebo prázdná, funkce je no-op a
 *                   vrací -1 (volající má použít stávající config dir/name).
 *
 * @pre @c dir_ptr != NULL, @c name_ptr != NULL.
 * @post Při úspěchu @c *dir_ptr a @c *name_ptr ukazují na nově alokované
 *       stringy odvozené z @p path; staré stringy jsou uvolněné.
 * @return 0 OK (dir/name přepsány), -1 prázdná/NULL @p path (beze změny).
 */
int reclife_redirect_path ( char **dir_ptr, char **name_ptr, const char *path );

/**
 * @brief Finalizace při ukončení subsystému (gate na save_on_exit).
 *
 * Byte-ekvivalent dosavadního @c cputrack_finalize gate: pokud
 * @p save_on_exit je nenulové a subsystém je aktivní (@c *active_ptr),
 * zavolá @c fn_stop (= flush + close). Jinak no-op.
 *
 * @note Tato funkce řeší POUZE zastavení běžícího recordingu. Uvolnění
 *       config zdrojů (g_free dir/name) si subsystém dělá sám ve svém
 *       finalize, protože je subsystem-specifické.
 *
 * @param d             Deskriptor subsystému (musí mít fn_stop).
 * @param save_on_exit  1 = flushnout při exitu, 0 = zahodit.
 *
 * @pre @c d != NULL, @c d->active_ptr != NULL, @c d->fn_stop != NULL.
 */
void reclife_finalize ( const st_RECLIFE_DESC *d, int save_on_exit );

#ifdef __cplusplus
}
#endif

#endif /* RECLIFE_H */
