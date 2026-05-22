/**
 * @file snapshot_io.h
 * @brief I/O vrstva snapshot systému — ZIP čtení/zápis přes minizip-ng
 *
 * Zapouzdřuje veškerou práci se ZIP archivem. Snapshot handlery
 * pracují výhradně přes toto API a nevidí minizip-ng přímo.
 */

#ifndef SNAPSHOT_IO_H
#define SNAPSHOT_IO_H

#include "snapshot.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle pro ZIP archiv */
typedef struct snapshot_io_t snapshot_io_t;


/**
 * Otevření archivu pro zápis (save)
 *
 * @param filepath Cesta k výstupnímu souboru
 * @param compression_level ZIP kompresní úroveň (0-9)
 * @return Handle nebo NULL při chybě
 */
snapshot_io_t *snapshot_io_open_write(const char *filepath, int compression_level);

/**
 * Otevření archivu pro čtení (load)
 *
 * @param filepath Cesta ke vstupnímu souboru
 * @return Handle nebo NULL při chybě
 */
snapshot_io_t *snapshot_io_open_read(const char *filepath);

/**
 * Zavření archivu a uvolnění zdrojů
 */
void snapshot_io_close(snapshot_io_t *io);

/**
 * Zápis binárních dat do archivu
 *
 * @param io Handle
 * @param entry_name Cesta v archivu (např. "memory/ram.bin")
 * @param data Ukazatel na data
 * @param size Velikost dat v bajtech
 * @return SNAPSHOT_OK při úspěchu
 */
en_SNAPSHOT_RESULT snapshot_io_write_bin(snapshot_io_t *io,
                                        const char *entry_name,
                                        const uint8_t *data,
                                        size_t size);

/**
 * Zápis XML řetězce do archivu
 *
 * @param io Handle
 * @param entry_name Cesta v archivu (např. "cpu/z80.xml")
 * @param xml_string XML řetězec (null-terminated)
 * @return SNAPSHOT_OK při úspěchu
 */
en_SNAPSHOT_RESULT snapshot_io_write_xml(snapshot_io_t *io,
                                        const char *entry_name,
                                        const char *xml_string);

/**
 * Čtení binárních dat z archivu (alokuje paměť)
 *
 * @param io Handle
 * @param entry_name Cesta v archivu
 * @param out_data Výstupní ukazatel na data (volající uvolní přes g_free)
 * @param out_size Výstupní velikost přečtených dat
 * @return SNAPSHOT_OK při úspěchu
 */
en_SNAPSHOT_RESULT snapshot_io_read_bin(snapshot_io_t *io,
                                       const char *entry_name,
                                       uint8_t **out_data,
                                       size_t *out_size);

/**
 * Čtení binárních dat do existujícího bufferu
 *
 * @param io Handle
 * @param entry_name Cesta v archivu
 * @param buffer Cílový buffer
 * @param buffer_size Očekávaná velikost dat
 * @return SNAPSHOT_OK při úspěchu
 */
en_SNAPSHOT_RESULT snapshot_io_read_bin_into(snapshot_io_t *io,
                                            const char *entry_name,
                                            uint8_t *buffer,
                                            size_t buffer_size);

/**
 * Čtení XML řetězce z archivu
 *
 * @param io Handle
 * @param entry_name Cesta v archivu
 * @param out_xml_string Výstupní ukazatel na řetězec (volající uvolní přes g_free)
 * @return SNAPSHOT_OK při úspěchu
 */
en_SNAPSHOT_RESULT snapshot_io_read_xml(snapshot_io_t *io,
                                       const char *entry_name,
                                       char **out_xml_string);

/**
 * Zjistí zda entry v archivu existuje
 *
 * @param io Handle
 * @param entry_name Cesta v archivu
 * @return true pokud entry existuje
 */
bool snapshot_io_entry_exists(snapshot_io_t *io, const char *entry_name);

/**
 * Vypočítá SHA-256 hash všech entries v archivu kromě "manifest.xml"
 *
 * Entries se zpracovávají v abecedním pořadí názvů.
 *
 * @param io Handle (musí být otevřen pro čtení)
 * @return Hex string (64 znaků + null), volající uvolní přes g_free. NULL při chybě.
 */
char *snapshot_io_compute_checksum(snapshot_io_t *io);


#ifdef __cplusplus
}
#endif

#endif /* SNAPSHOT_IO_H */
