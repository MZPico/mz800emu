/**
 * @file hex_view_backend.h
 * @brief Backend adapter interface pro core hex view renderer.
 *
 * Memory Browser hex view kód NESMÍ volat dbgapi_regions_read/write přímo.
 * Místo toho používá toto opaque interface, takže budoucí konzumenti
 * (Media Browser pro DSK/MZF soubory, VRAM Viewer, Glyph Editor, VRAM
 * Visual Editor - viz rozbory/03-MEDIA-BROWSER-FUTURE.md) mohou reusovat
 * stejný core hex render lib bez závislosti na běžícím emulátoru.
 *
 * Pro Memory Browser V0 existuje jediný backend implementující toto
 * rozhraní - emu region backend wrapper v membrowser_io.cpp, který
 * deleguje na dbgapi_regions_*. Žádný direct dbgapi_regions_* call
 * v membrowser_hexview.cpp.
 *
 * ----------------------------- License -------------------------------------
 *
 * GPL-3.0-or-later.
 *
 * ---------------------------------------------------------------------------
 */

#ifndef HEX_VIEW_BACKEND_H
#define HEX_VIEW_BACKEND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Backend adapter pro core hex view renderer.
 *
 * Caller nastaví ctx (opaque user pointer) + řadu function pointers.
 * Core hex view zná pouze toto rozhraní, nikdy nevolá konkrétní backend
 * funkce přímo. Pro Memory Browser je ctx = (void*)(intptr_t)region_id.
 *
 * Lifetime: caller vlastní strukturu + ctx; backend ji nikdy nealokuje
 * ani nefree. Strukturu lze sdílet read-only mezi více ImGui render
 * volání (nepředpokládá se mutace během renderu).
 *
 * Invariants:
 *   - read_bytes != NULL vždy
 *   - total_size != NULL vždy
 *   - write_bytes = NULL znamená read-only backend
 *   - region_label = NULL znamená "bez label badge"
 */
typedef struct st_HEX_VIEW_BACKEND
{
    /** Opaque user kontext. Memory Browser: (void*)(intptr_t)region_id. */
    void *ctx;

    /**
     * @brief Přečte len bajtů z offset do out bufferu.
     *
     * @param ctx     User kontext (jako v st_HEX_VIEW_BACKEND.ctx).
     * @param offset  Offset v rámci backendu (0..total_size-1).
     * @param out     Buffer pro výsledek (min len bajtů).
     * @param len     Počet bajtů k přečtení.
     * @return Skutečný počet přečtených bajtů (0..len) nebo -1 při chybě.
     *
     * @pre out != NULL, len > 0
     * @post Žádný side-effect na backend storage.
     */
    int (*read_bytes) ( void *ctx, uint64_t offset, uint8_t *out, uint32_t len );

    /**
     * @brief Zapíše len bajtů z in bufferu do offset.
     *
     * @param ctx     User kontext.
     * @param offset  Offset v rámci backendu.
     * @param in      Buffer s daty (min len bajtů).
     * @param len     Počet bajtů k zapsání.
     * @return Skutečný počet zapsaných bajtů (0..len) nebo -1 při chybě
     *         (R-only backend, mimo rozsah, atd.).
     *
     * @pre in != NULL, len > 0
     * @note Smí být NULL = backend je read-only (UI by mělo edit zakázat).
     */
    int (*write_bytes) ( void *ctx, uint64_t offset, const uint8_t *in, uint32_t len );

    /**
     * @brief Vrací celkovou velikost backendu v bajtech.
     *
     * @param ctx User kontext.
     * @return Velikost > 0 nebo 0 při chybě (disconnect / not ready).
     */
    uint64_t (*total_size) ( void *ctx );

    /**
     * @brief Volitelný label pro per-row badge ("ROM lower", "VRAM-I", ...).
     *
     * Memory Browser V0 ho nepoužívá (single-region view bez split label).
     * V1+ pro Logical Z80 view bude generovat per-row banking label.
     *
     * @param ctx     User kontext.
     * @param offset  Offset v rámci backendu.
     * @return Pointer na statický řetězec nebo NULL = bez label.
     */
    const char * (*region_label) ( void *ctx, uint64_t offset );
} st_HEX_VIEW_BACKEND;

#ifdef __cplusplus
}
#endif

#endif /* HEX_VIEW_BACKEND_H */
