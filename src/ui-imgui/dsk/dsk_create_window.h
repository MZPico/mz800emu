/**
 * @file dsk_create_window.h
 * @brief Informační okno - DSK tools přemístěny do samostatné aplikace mzdisk
 *
 * Původní DSK Create dialog (=interní GUI pro vytváření DSK image souborů)
 * byl odstraněn. Funkcionalita je nyní v samostatné aplikaci `mzdisk`:
 *   https://github.com/michalhucik/mzdisk
 *
 * Toto okno už jen zobrazí informaci s URL + tlačítka Copy URL / Open URL / OK.
 */

#ifndef DSK_CREATE_WINDOW_H
#define DSK_CREATE_WINDOW_H

#ifdef __cplusplus
extern "C"
{
#endif
    void imgui_dsk_create_window(bool *p_open);
#ifdef __cplusplus
}
#endif

#endif /* DSK_CREATE_WINDOW_H */
