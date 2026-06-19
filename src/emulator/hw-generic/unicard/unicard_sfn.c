/**
 * @file unicard_sfn.c
 * @brief Implementace generátoru FatFS 8.3 short jména - viz unicard_sfn.h.
 *
 * Věrný port relevantních částí FatFS R0.13a (zdroj firmware Unicard,
 * unicard3-FW_2026-05-03/src/ff.c):
 *   - create_name()   -> sfn_create_body()    (sestavení SFN těla + NS_*)
 *   - gen_numname()   -> sfn_gen_numname()     (~N / hash tail)
 *   - get_fileinfo()  -> sfn_body_to_display() (display tvar + prázdný alias)
 *
 * Konfigurace FW (ffconf.h): FF_USE_LFN=1, FF_MAX_LFN=32, FF_FS_EXFAT=0,
 * FF_LFN_UNICODE=0, FF_CODE_PAGE=852 (SBCS, žádné DBC).
 *
 * Omezení proti reálnému FW: non-ASCII bajty (UTF-8 multibyte, diakritika)
 * tento port nepřevádí přes CP852 ExCvt tabulku na velká OEM písmena;
 * místo toho je značí jako lossy a nahrazuje '_'. Pro ASCII jména
 * (typický obsah testovacích MZF) je výsledek bajtově shodný s FW.
 * [neověřeno pro CP852 diakritiku - viz TODO níže]
 *
 * @copyright GPLv3
 */

#include "unicard_sfn.h"

#include <string.h>

/* NS_* příznaky stavu jména (shodné s ff.c). */
#define SFN_NS_LOSS  0x01   /**< mimo 8.3 formát -> nutný numbered SFN */
#define SFN_NS_LFN   0x02   /**< nutná LFN directory entry */
#define SFN_NS_BODY  0x08   /**< NT case flag: tělo jen malá písmena */
#define SFN_NS_EXT   0x10   /**< NT case flag: přípona jen malá písmena */

#define SFN_DDEM     0xE5   /**< značka smazané entry (DIR_Name[0]) */
#define SFN_RDDEM    0x05   /**< náhrada za znak kolidující s DDEM */

#define SFN_BODY_LEN 11     /**< délka SFN těla (8 base + 3 ext) */

/* Velká/malá ASCII písmena (shodné s ff.c IsUpper/IsLower). */
#define SFN_IS_UPPER(c)  ( (c) >= 'A' && (c) <= 'Z' )
#define SFN_IS_LOWER(c)  ( (c) >= 'a' && (c) <= 'z' )

/**
 * @brief Zjistí, zda znak @p chr je obsažen v ASCII řetězci @p str.
 *
 * Port ff.c chk_chr(). Nepoužívat s chr == 0 (našel by terminátor).
 *
 * @param str  prohledávaný NUL-terminovaný řetězec
 * @param chr  hledaný znak (kód)
 * @return nenulové pokud obsažen, jinak 0
 */
static int sfn_chk_chr ( const char *str, int chr ) {
    while ( *str && *str != chr ) str++;
    return *str == chr;
}

/**
 * @brief Vytvoří numbered SFN tělo (vloží "~N" před příponu).
 *
 * Věrný port ff.c gen_numname(). Při @p seq > 5 generuje místo
 * sekvenčního čísla CRC hash z LFN (jako FatFS). CP852 je SBCS, takže
 * DBC větev originálu je vynechána.
 *
 * @param dst  výstup, SFN_BODY_LEN bajtů (numbered tělo)
 * @param src  vstupní SFN tělo, SFN_BODY_LEN bajtů
 * @param lfn  LFN v UTF-16 (NUL-terminované), zdroj hashe při seq > 5
 * @param seq  pořadové číslo (1..)
 */
static void sfn_gen_numname ( unsigned char *dst, const unsigned char *src,
                              const gunichar2 *lfn, unsigned int seq ) {
    unsigned char ns[8], c;
    unsigned int i, j;
    guint32 sr;
    guint16 wc;

    memcpy ( dst, src, SFN_BODY_LEN );

    if ( seq > 5 ) {    /* mnoho kolizí -> hash místo sekvence */
        sr = seq;
        while ( *lfn ) {
            wc = *lfn++;
            for ( i = 0; i < 16; i++ ) {
                sr = ( sr << 1 ) + ( wc & 1 );
                wc >>= 1;
                if ( sr & 0x10000 ) sr ^= 0x11021;
            }
        }
        seq = (unsigned int) sr;
    }

    /* itoa (hexadecimálně) do ns[], odzadu */
    i = 7;
    do {
        c = (unsigned char) ( ( seq % 16 ) + '0' );
        if ( c > '9' ) c += 7;
        ns[i--] = c;
        seq /= 16;
    } while ( seq );
    ns[i] = '~';

    /* připoj číslo k tělu SFN (CP852 SBCS - bez DBC přeskoku) */
    for ( j = 0; j < i && dst[j] != ' '; j++ ) {
        /* prázdné: u SBCS nepřeskakujeme DBC první bajt */
    }
    do {
        dst[j++] = ( i < 8 ) ? ns[i++] : ' ';
    } while ( j < 8 );
}

/**
 * @brief Sestaví SFN tělo (11 bajtů) a NS_* příznaky z UTF-16 jména.
 *
 * Věrný port LFN větve ff.c create_name() pro jediný segment (basename
 * bez oddělovačů cesty). Dot-entry (FF_FS_RPATH) větev vynechána - volající
 * "."/".." řeší zvlášť.
 *
 * @param lfn       LFN v UTF-16, NUL-terminované (může být oříznuto trailing
 *                  mezerami/tečkami funkcí - viz níže)
 * @param di_in     délka LFN v UTF-16 jednotkách
 * @param out_body  výstup, SFN_BODY_LEN bajtů SFN těla (mezerami zarovnané)
 * @param out_flags výstup, NS_* příznaky
 */
static void sfn_create_body ( gunichar2 *lfn, unsigned int di_in,
                              unsigned char *out_body, unsigned char *out_flags ) {
    unsigned char b, cf;
    unsigned int i, ni, si, di;
    guint16 wc;

    di = di_in;
    cf = 0;

    /* odřízni trailing mezery a tečky */
    while ( di ) {
        wc = lfn[di - 1];
        if ( wc != ' ' && wc != '.' ) break;
        di--;
    }
    lfn[di] = 0;

    /* SFN v directory form */
    for ( si = 0; lfn[si] == ' '; si++ ) ;          /* přeskoč leading mezery */
    if ( si > 0 || lfn[si] == '.' ) cf |= SFN_NS_LOSS | SFN_NS_LFN;
    while ( di > 0 && lfn[di - 1] != '.' ) di--;     /* najdi poslední tečku */

    memset ( out_body, ' ', SFN_BODY_LEN );
    i = b = 0; ni = 8;
    for ( ;; ) {
        wc = lfn[si++];
        if ( wc == 0 ) break;
        if ( wc == ' ' || ( wc == '.' && si != di ) ) {  /* odstraň vnořené mezery/tečky */
            cf |= SFN_NS_LOSS | SFN_NS_LFN;
            continue;
        }

        if ( i >= ni || si == di ) {        /* konec pole? */
            if ( ni == SFN_BODY_LEN ) {     /* přetečení přípony */
                cf |= SFN_NS_LOSS | SFN_NS_LFN;
                break;
            }
            if ( si != di ) cf |= SFN_NS_LOSS | SFN_NS_LFN;  /* přetečení těla */
            if ( si > di ) break;                            /* bez přípony */
            si = di; i = 8; ni = SFN_BODY_LEN; b <<= 2;      /* vstup do přípony */
            continue;
        }

        if ( wc >= 0x80 ) {     /* non-ASCII */
            /* TODO[neověřeno]: reálný FatFS převede přes ff_uni2oem(852) +
             * ExCvt na velké OEM písmeno. Zde konzervativně lossy + '_'. */
            cf |= SFN_NS_LFN;
            wc = '_';
            cf |= SFN_NS_LOSS;
        } else {
            if ( wc == 0 || sfn_chk_chr ( "+,;=[]", wc ) ) {  /* nelegální SFN znak */
                wc = '_';
                cf |= SFN_NS_LOSS | SFN_NS_LFN;
            } else {
                if ( SFN_IS_UPPER ( wc ) ) b |= 2;
                if ( SFN_IS_LOWER ( wc ) ) { b |= 1; wc -= 0x20; }
            }
        }
        out_body[i++] = (unsigned char) wc;
    }

    if ( out_body[0] == SFN_DDEM ) out_body[0] = SFN_RDDEM;

    if ( ni == 8 ) b <<= 2;     /* posun case flagů pokud bez přípony */
    if ( ( b & 0x0C ) == 0x0C || ( b & 0x03 ) == 0x03 ) cf |= SFN_NS_LFN;  /* smíšená velikost */
    if ( !( cf & SFN_NS_LFN ) ) {   /* čisté 8.3 bez non-ASCII -> NT case flagy */
        if ( b & 0x01 ) cf |= SFN_NS_EXT;
        if ( b & 0x04 ) cf |= SFN_NS_BODY;
    }

    *out_flags = cf;
}

/**
 * @brief Převede 11-bajtové SFN tělo na display tvar s tečkou.
 *
 * Port altname části ff.c get_fileinfo(). Vkládá tečku před příponu,
 * přeskakuje výplňové mezery, obnovuje RDDEM -> DDEM.
 *
 * @param body  vstupní SFN tělo, SFN_BODY_LEN bajtů
 * @param out   výstup, min. 13 bajtů, NUL-terminovaný display alias
 */
static void sfn_body_to_display ( const unsigned char *body, char *out ) {
    unsigned int si = 0, di = 0;
    unsigned char wc;

    while ( si < SFN_BODY_LEN ) {
        wc = body[si++];
        if ( wc == ' ' ) continue;          /* přeskoč výplň */
        if ( wc == SFN_RDDEM ) wc = SFN_DDEM;
        if ( si == 9 && di < 12 ) out[di++] = '.';  /* tečka před příponu */
        out[di++] = (char) wc;
    }
    out[di] = '\0';
}

/**
 * @brief Převede SFN tělo na display tvar s aplikovanou velikostí písmen
 *        (uc1 / FatFS R0.09 get_fileinfo).
 *
 * Na rozdíl od UC3 (altname, vždy velká, prázdné pro čisté 8.3) plní pole
 * VŽDY a aplikuje NT case flagy: NS_BODY -> tělo malými, NS_EXT -> přípona
 * malými. Velikost se mění jen tam, kde byl příznak nastaven (tj. původní
 * část byla celá malými písmeny - uniform case). Port smyčky z R0.09
 * get_fileinfo (`if ((nt & NS_BODY) && IsUpper(c)) c += 0x20`).
 *
 * @param body  SFN tělo, SFN_BODY_LEN bajtů
 * @param cf    NS_* příznaky z sfn_create_body (NS_BODY/NS_EXT)
 * @param out   výstup, min. 13 bajtů, NUL-terminovaný
 */
static void sfn_body_to_display_uc1 ( const unsigned char *body, unsigned char cf, char *out ) {
    unsigned int i, di = 0;
    unsigned char c;

    for ( i = 0; i < 8; i++ ) {         /* tělo */
        c = body[i];
        if ( c == ' ' ) break;
        if ( c == SFN_RDDEM ) c = SFN_DDEM;
        if ( ( cf & SFN_NS_BODY ) && SFN_IS_UPPER ( c ) ) c += 0x20;
        out[di++] = (char) c;
    }
    if ( body[8] != ' ' ) {             /* přípona */
        out[di++] = '.';
        for ( i = 8; i < SFN_BODY_LEN; i++ ) {
            c = body[i];
            if ( c == ' ' ) break;
            if ( c == SFN_RDDEM ) c = SFN_DDEM;
            if ( ( cf & SFN_NS_EXT ) && SFN_IS_UPPER ( c ) ) c += 0x20;
            out[di++] = (char) c;
        }
    }
    out[di] = '\0';
}

void unicard_sfn_ctx_init ( st_UNICARD_SFN_CTX *ctx ) {
    ctx->assigned = g_ptr_array_new_with_free_func ( g_free );
}

void unicard_sfn_ctx_clear ( st_UNICARD_SFN_CTX *ctx ) {
    if ( ctx == NULL ) return;
    if ( ctx->assigned != NULL ) {
        g_ptr_array_free ( ctx->assigned, TRUE );
        ctx->assigned = NULL;
    }
}

/**
 * @brief Zjistí, zda je SFN tělo @p body již registrováno v kontextu.
 *
 * @param ctx   kolizní kontext
 * @param body  SFN tělo, SFN_BODY_LEN bajtů
 * @return TRUE pokud kolize (již přiděleno), jinak FALSE
 */
static gboolean sfn_ctx_collides ( const st_UNICARD_SFN_CTX *ctx, const unsigned char *body ) {
    guint k;
    for ( k = 0; k < ctx->assigned->len; k++ ) {
        const unsigned char *prev = g_ptr_array_index ( ctx->assigned, k );
        if ( memcmp ( prev, body, SFN_BODY_LEN ) == 0 ) return TRUE;
    }
    return FALSE;
}

/**
 * @brief Zaregistruje SFN tělo @p body do kolizního kontextu (g_memdup).
 */
static void sfn_ctx_register ( st_UNICARD_SFN_CTX *ctx, const unsigned char *body ) {
    unsigned char *copy = g_malloc ( SFN_BODY_LEN );
    memcpy ( copy, body, SFN_BODY_LEN );
    g_ptr_array_add ( ctx->assigned, copy );
}

void unicard_sfn_make ( const char *name_utf8, en_UNICARD_SFN_STYLE style,
                        st_UNICARD_SFN_CTX *ctx, char out_altname[13], gboolean *out_has_lfn ) {
    gunichar2 lfn[64];          /* FF_MAX_LFN=32; rezerva na delší vstup */
    unsigned char body[SFN_BODY_LEN];
    unsigned char final_body[SFN_BODY_LEN];
    unsigned char cf;

    out_altname[0] = '\0';
    if ( out_has_lfn ) *out_has_lfn = FALSE;

    /* UTF-8 -> UTF-16. Příliš dlouhé / nedekódovatelné -> prázdný alias. */
    glong items = 0;
    gunichar2 *u16 = g_utf8_to_utf16 ( name_utf8, -1, NULL, &items, NULL );
    if ( u16 == NULL || items <= 0 ) {
        if ( u16 ) g_free ( u16 );
        return;
    }
    unsigned int di = (unsigned int) items;
    if ( di >= G_N_ELEMENTS ( lfn ) ) di = G_N_ELEMENTS ( lfn ) - 1;
    memcpy ( lfn, u16, di * sizeof ( gunichar2 ) );
    lfn[di] = 0;
    g_free ( u16 );

    sfn_create_body ( lfn, di, body, &cf );

    /* NS_LFN určuje, zda jméno potřebuje LFN entry (lossy / smíšená
     * velikost / non-ASCII). Reálné HW podle toho plní pole LFN. */
    if ( out_has_lfn ) *out_has_lfn = ( cf & SFN_NS_LFN ) != 0;

    if ( cf & SFN_NS_LOSS ) {
        /* numbered SFN - najdi nejnižší nekolidující ~N (port dir_register) */
        unsigned int n;
        memcpy ( final_body, body, SFN_BODY_LEN );
        for ( n = 1; n < 100; n++ ) {
            sfn_gen_numname ( final_body, body, lfn, n );
            if ( !sfn_ctx_collides ( ctx, final_body ) ) break;
        }
    } else {
        memcpy ( final_body, body, SFN_BODY_LEN );
    }

    sfn_ctx_register ( ctx, final_body );

    if ( style == UNICARD_SFN_STYLE_UC1 ) {
        /* uc1 (R0.09 fname): pole vždy vyplněné, 8.3 s aplikovanou
         * velikostí písmen. */
        sfn_body_to_display_uc1 ( final_body, cf, out_altname );
        return;
    }

    /* uc3 (R0.13a altname): "opravený" uc3 plní pole S vždy 8.3 aliasem
     * (velká písmena). FatFS empty rule (prázdné altname pro čisté 8.3 ve
     * velkých písmenech) ZÁMĚRNĚ NEpoužíváme - idealizované chování, aby
     * shortname nikdy nechybělo (rozhodnutí Michal, "jak by mělo být kdyby
     * FW byl OK"). Reálný uc3 by zde dal prázdné / vynulovaný 1. znak. */
    sfn_body_to_display ( final_body, out_altname );
}
