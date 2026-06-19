/**
 * @file unicard_sfn.h
 * @brief Generátor FatFS-kompatibilního 8.3 short jména (SFN aliasu).
 *
 * Emulace Unicard čte hostitelský adresář přes GLib (g_dir), jehož FS
 * (NTFS/ext4/...) nedrží FAT 8.3 short jména. Reálná Unicarta běží skutečný
 * FatFS, který do pole FILINFO.altname (guest offset 9) plní 8.3 alias
 * vygenerovaný z LFN (např. "Batman Demo.mzf" -> "BATMAN~1.MZF"). Tento
 * modul ten alias dogeneruje shodným algoritmem jako FatFS, aby se emu
 * choval věrně reálnému HW.
 *
 * Algoritmus je věrný port FatFS R0.13a (zdroj firmware Unicard,
 * src/ff.c): funkcí create_name() (sestavení 8.3 SFN těla + NS_* příznaky),
 * gen_numname() (~N / hash tail při kolizích) a altname části
 * get_fileinfo() (převod 11-bytového SFN na display tvar s tečkou, vč.
 * vyprázdnění aliasu pro čistě 8.3-velká jména bez case info).
 *
 * Důležitý fakt o věrnosti (potvrzeno zdrojem FW, ne dovozeno): reálný
 * FatFS vrací altname PRÁZDNÝ pro jméno, které samo je validní 8.3 ve
 * velkých písmenech (žádné LFN ani NT case info), např. "Y2K.MZF" nebo
 * "SAPO-P.MZF". Tento modul to chování replikuje (out_altname = "").
 *
 * Kolizní ~N číslování je deterministické v rámci jednoho adresáře:
 * jména se zpracovávají v pořadí iterace a každé přidělené 11-bytové SFN
 * se registruje do st_UNICARD_SFN_CTX. Pořadí (a tím konkrétní ~N číslo)
 * nemusí odpovídat fyzické FAT kartě (závisí na historii vytváření
 * souborů na médiu) - klíčová je platnost a determinismus aliasu.
 *
 * Code page: FatFS FW je konfigurován na CP852 (FF_CODE_PAGE=852, SBCS).
 * Tento port řeší věrně ASCII jména; non-ASCII bajty (UTF-8 multibyte,
 * diakritika) zatím nepřevádí na CP852 a značí jméno jako lossy. Viz
 * unicard_sfn.c [neověřeno pro CP852 diakritiku].
 *
 * @copyright GPLv3
 */

#ifndef UNICARD_SFN_H
#define UNICARD_SFN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <glib.h>

/**
 * @brief Kontext pro kolizní ~N číslování SFN aliasů v rámci adresáře.
 *
 * Drží množinu již přidělených 11-bytových SFN těl (bez tečky,
 * NUL-terminovaná, vždy 11 znaků mezerami nezarovnaná - viz formát níže),
 * proti které unicard_sfn_make() kontroluje kolize a inkrementuje pořadové
 * číslo ~N. Inicializovat unicard_sfn_ctx_init(), uvolnit
 * unicard_sfn_ctx_clear().
 *
 * Invariant: po init je @ref assigned platné (neprázdné GPtrArray) až do
 * clear. Ownership prvků náleží ctx (g_strdup kopie).
 */
/**
 * @brief Styl plnění pole offset 9, per-firmware Unicard.
 *
 * Oba firmware běží jiný FatFS s jinou sémantikou pole na guest offsetu 9:
 *
 * - @ref UNICARD_SFN_STYLE_UC3: Unicard3 (FatFS R0.13a). Offset 9 =
 *   FatFS `altname` (SFN). Vždy velká písmena. Pro jméno, které je samo
 *   validní 8.3 ve velkých písmenech bez LFN/case info, je pole PRÁZDNÉ
 *   (`get_fileinfo` altname[0]=0).
 *
 * - @ref UNICARD_SFN_STYLE_UC1: MZ800UKP1 rev 60 (FatFS R0.09). Offset 9 =
 *   FatFS `fname` (8.3). VŽDY vyplněné, s aplikovanou velikostí písmen z NT
 *   flagů (`seesharp.mzf` zůstane malými). Lossy jména -> `BASE~N.EXT`.
 *
 * Příklad rozdílu (`seesharp.mzf` / `SAPO-P.MZF`):
 *   UC1 -> `seesharp.mzf` / `SAPO-P.MZF`
 *   UC3 -> `SEESHARP.MZF` / "" (prázdné)
 */
typedef enum en_UNICARD_SFN_STYLE {
    UNICARD_SFN_STYLE_UC3 = 0,  /**< FatFS R0.13a altname (uppercase, prázdné pro čisté 8.3) */
    UNICARD_SFN_STYLE_UC1 = 1   /**< FatFS R0.09 fname (8.3 s case, vždy vyplněné) */
} en_UNICARD_SFN_STYLE;

typedef struct st_UNICARD_SFN_CTX {
    GPtrArray *assigned;   /**< pole g_strdup kopií 11-znakových SFN těl */
} st_UNICARD_SFN_CTX;

/**
 * @brief Inicializuje prázdný kolizní kontext.
 *
 * @param ctx kontext k inicializaci (nesmí být NULL)
 *
 * @post ctx->assigned je platné prázdné GPtrArray.
 */
extern void unicard_sfn_ctx_init ( st_UNICARD_SFN_CTX *ctx );

/**
 * @brief Uvolní kolizní kontext a všechny registrované SFN.
 *
 * @param ctx kontext k uvolnění (NULL je no-op)
 *
 * @post ctx->assigned == NULL.
 */
extern void unicard_sfn_ctx_clear ( st_UNICARD_SFN_CTX *ctx );

/**
 * @brief Vygeneruje FatFS 8.3 alias (display tvar) z UTF-8 jména.
 *
 * Replikuje FatFS create_name + gen_numname + get_fileinfo. Sestavení SFN
 * těla a ~N kolizní číslování jsou pro oba styly společné; liší se jen
 * převod 11-bytového SFN na výstup dle @p style:
 *
 * - @ref UNICARD_SFN_STYLE_UC3 (FatFS R0.13a altname): velká písmena;
 *   prázdný řetězec pro čistě 8.3 jméno ve velkých písmenech bez case info
 *   (`Y2K.MZF` -> "").
 * - @ref UNICARD_SFN_STYLE_UC1 (FatFS R0.09 fname): vždy vyplněné, s
 *   aplikovanou velikostí písmen z NT flagů (`seesharp.mzf` -> `seesharp.mzf`,
 *   `Y2K.MZF` -> `Y2K.MZF`).
 *
 * Pro lossy jména (dlouhá, s mezerou, mimo 8.3) oba styly vrací "BASE~N.EXT",
 * kde N je nejnižší pořadové číslo nekolidující s @p ctx (při >5 kolizích
 * hash tail jako FatFS).
 *
 * Vygenerované 11-bytové SFN tělo se vždy zaregistruje do @p ctx (i pro
 * případ prázdného aliasu - obsazuje kolizní slot), aby další jména
 * dostala správné ~N číslo.
 *
 * @param name_utf8     vstupní jméno (basename, UTF-8/ASCII, bez cesty)
 * @param style         styl plnění offset 9 (per-firmware)
 * @param ctx           kolizní kontext (nesmí být NULL); modifikován
 * @param out_altname   výstupní buffer min. 13 bytů; NUL-terminovaný
 *                      display alias nebo "" (jen UC3 pro čisté 8.3)
 *
 * @pre name_utf8 != NULL, ctx inicializován, out_altname má >= 13 bytů.
 * @post out_altname obsahuje NUL-terminovaný řetězec délky 0..12.
 */
extern void unicard_sfn_make ( const char *name_utf8, en_UNICARD_SFN_STYLE style, st_UNICARD_SFN_CTX *ctx, char out_altname[13] );

#ifdef __cplusplus
}
#endif

#endif /* UNICARD_SFN_H */
