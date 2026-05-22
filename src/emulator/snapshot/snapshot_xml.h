/**
 * @file snapshot_xml.h
 * @brief XML writer (vlastní) + reader (nad GLib GMarkup)
 *
 * Writer automaticky přidává atribut type ke každému elementu s hodnotou.
 * Reader parsuje XML přes GMarkup a staví interní DOM strom pro navigaci.
 */

#ifndef SNAPSHOT_XML_H
#define SNAPSHOT_XML_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


/* ========================================================================= */
/*                              XML Writer                                   */
/* ========================================================================= */

/**
 * Opaque handle pro XML writer
 */
typedef struct snapshot_xml_writer_t snapshot_xml_writer_t;

/**
 * Vytvoření nového writeru
 */
snapshot_xml_writer_t *snapshot_xml_writer_new(void);

/**
 * Zápis XML hlavičky <?xml version="1.0" encoding="UTF-8"?>
 */
void snapshot_xml_write_header(snapshot_xml_writer_t *w);

/**
 * Otevření elementu (bez atributů)
 */
void snapshot_xml_open_element(snapshot_xml_writer_t *w, const char *name);

/**
 * Otevření elementu s jedním atributem
 */
void snapshot_xml_open_element_attr(snapshot_xml_writer_t *w, const char *name,
                                    const char *attr_name, const char *attr_value);

/**
 * Zavření aktuálního elementu
 */
void snapshot_xml_close_element(snapshot_xml_writer_t *w);

/**
 * Zápis elementu s hodnotou a automatickým atributem type
 */
void snapshot_xml_write_string(snapshot_xml_writer_t *w, const char *name, const char *value);
void snapshot_xml_write_int(snapshot_xml_writer_t *w, const char *name, int value);
void snapshot_xml_write_uint(snapshot_xml_writer_t *w, const char *name, unsigned value);
void snapshot_xml_write_hex8(snapshot_xml_writer_t *w, const char *name, uint8_t value);
void snapshot_xml_write_hex16(snapshot_xml_writer_t *w, const char *name, uint16_t value);
void snapshot_xml_write_hex32(snapshot_xml_writer_t *w, const char *name, uint32_t value);
void snapshot_xml_write_uint64(snapshot_xml_writer_t *w, const char *name, uint64_t value);
void snapshot_xml_write_bool(snapshot_xml_writer_t *w, const char *name, bool value);

/**
 * Získání výsledného XML řetězce a destrukce writeru
 *
 * @return XML řetězec — volající musí uvolnit přes g_free()
 */
char *snapshot_xml_writer_finish(snapshot_xml_writer_t *w);


/* ========================================================================= */
/*                              XML Reader                                   */
/* ========================================================================= */

/**
 * Opaque handle pro XML reader
 */
typedef struct snapshot_xml_reader_t snapshot_xml_reader_t;

/**
 * Vytvoření readeru z XML řetězce
 *
 * Parsuje XML přes GLib GMarkup a staví interní DOM strom.
 *
 * @param xml_string XML řetězec
 * @return Reader handle nebo NULL při chybě parsování
 */
snapshot_xml_reader_t *snapshot_xml_reader_new(const char *xml_string);

/**
 * Destrukce readeru a uvolnění DOM stromu
 */
void snapshot_xml_reader_free(snapshot_xml_reader_t *r);

/**
 * Navigace — vstup do child elementu aktuálního uzlu
 *
 * @param r Reader handle
 * @param name Název child elementu
 * @return true pokud element existuje a vstoupili jsme do něj
 */
bool snapshot_xml_enter_element(snapshot_xml_reader_t *r, const char *name);

/**
 * Navigace — návrat na rodičovský element
 */
void snapshot_xml_leave_element(snapshot_xml_reader_t *r);

/**
 * Čtení hodnoty child elementu (bez nutnosti enter/leave)
 *
 * Funkce hledá child element s daným názvem v aktuálním elementu,
 * přečte jeho textovou hodnotu a konvertuje na požadovaný typ.
 *
 * @return true pokud element existuje a hodnota byla úspěšně přečtena
 */
bool snapshot_xml_read_string(snapshot_xml_reader_t *r, const char *name, char **out_value);
bool snapshot_xml_read_int(snapshot_xml_reader_t *r, const char *name, int *out_value);
bool snapshot_xml_read_uint(snapshot_xml_reader_t *r, const char *name, unsigned *out_value);
bool snapshot_xml_read_hex8(snapshot_xml_reader_t *r, const char *name, uint8_t *out_value);
bool snapshot_xml_read_hex16(snapshot_xml_reader_t *r, const char *name, uint16_t *out_value);
bool snapshot_xml_read_hex32(snapshot_xml_reader_t *r, const char *name, uint32_t *out_value);
bool snapshot_xml_read_uint64(snapshot_xml_reader_t *r, const char *name, uint64_t *out_value);
bool snapshot_xml_read_bool(snapshot_xml_reader_t *r, const char *name, bool *out_value);

/**
 * Čtení atributu aktuálního elementu
 *
 * @param r Reader handle
 * @param attr_name Název atributu
 * @param out_value Výstupní hodnota — volající musí uvolnit přes g_free()
 * @return true pokud atribut existuje
 */
bool snapshot_xml_read_attr(snapshot_xml_reader_t *r, const char *attr_name, char **out_value);


/* ========================================================================= */
/*                       Logovací makra                                      */
/* ========================================================================= */

#define SNAP_WARN(component, fmt, ...) \
    fprintf(stderr, "Snapshot [%s]: WARN: " fmt "\n", component, ##__VA_ARGS__)

#define SNAP_ERR(component, fmt, ...) \
    fprintf(stderr, "Snapshot [%s]: ERR: " fmt "\n", component, ##__VA_ARGS__)


#ifdef __cplusplus
}
#endif

#endif /* SNAPSHOT_XML_H */
