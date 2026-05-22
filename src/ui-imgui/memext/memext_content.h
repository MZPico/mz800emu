/**
 * @file memext_content.h
 * @brief Dialogy pro nahrání/uložení obsahu MemExt paměti (RAM/FLASH)
 */

#ifndef MEMEXT_CONTENT_H
#define MEMEXT_CONTENT_H

/* Typ paměti pro parametrizaci dialogů */
typedef enum {
    MEMEXT_CONTENT_RAM = 0,
    MEMEXT_CONTENT_FLASH = 1
} en_MemextContentType;

#ifdef __cplusplus
extern "C"
{
#endif
    void imgui_memext_load_dialog(void);
    void imgui_memext_save_dialog(void);

    /* Otevření dialogů z menu */
    void imgui_memext_open_load(en_MemextContentType type);
    void imgui_memext_open_save(en_MemextContentType type);
#ifdef __cplusplus
}
#endif

#endif /* MEMEXT_CONTENT_H */
