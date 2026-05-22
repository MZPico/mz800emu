/**
 * @file snapshot_xml.c
 * @brief XML writer (vlastní) + reader (nad GLib GMarkup)
 *
 * Writer: Generuje validní XML s automatickým type atributem.
 * Reader: GMarkup SAX parser → interní DOM strom → navigační API.
 */

#include "snapshot_xml.h"

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>


/* ========================================================================= */
/*                              XML Writer                                   */
/* ========================================================================= */

#define WRITER_INDENT_SPACES  2
#define WRITER_MAX_DEPTH      32

struct snapshot_xml_writer_t {
    GString *buffer;
    int indent_level;
    /* Zásobník názvů otevřených elementů pro automatické zavírání */
    char *element_stack[WRITER_MAX_DEPTH];
    int stack_top;
};


/**
 * Zapíše odsazení na aktuální úrovni
 */
static void writer_indent(snapshot_xml_writer_t *w)
{
    int spaces = w->indent_level * WRITER_INDENT_SPACES;
    for (int i = 0; i < spaces; i++) {
        g_string_append_c(w->buffer, ' ');
    }
}


/**
 * Escapování speciálních XML znaků v textu
 */
static char *xml_escape_text(const char *text)
{
    if (!text) return g_strdup("");

    GString *escaped = g_string_new(NULL);
    for (const char *p = text; *p; p++) {
        switch (*p) {
            case '&':  g_string_append(escaped, "&amp;");  break;
            case '<':  g_string_append(escaped, "&lt;");   break;
            case '>':  g_string_append(escaped, "&gt;");   break;
            case '"':  g_string_append(escaped, "&quot;"); break;
            case '\'': g_string_append(escaped, "&apos;"); break;
            default:   g_string_append_c(escaped, *p);     break;
        }
    }
    return g_string_free(escaped, FALSE);
}


snapshot_xml_writer_t *snapshot_xml_writer_new(void)
{
    snapshot_xml_writer_t *w = g_new0(snapshot_xml_writer_t, 1);
    w->buffer = g_string_new(NULL);
    w->indent_level = 0;
    w->stack_top = -1;
    return w;
}


void snapshot_xml_write_header(snapshot_xml_writer_t *w)
{
    g_string_append(w->buffer, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
}


void snapshot_xml_open_element(snapshot_xml_writer_t *w, const char *name)
{
    writer_indent(w);
    g_string_append_printf(w->buffer, "<%s>\n", name);

    /* Push na zásobník */
    w->stack_top++;
    if (w->stack_top < WRITER_MAX_DEPTH) {
        w->element_stack[w->stack_top] = g_strdup(name);
    }
    w->indent_level++;
}


void snapshot_xml_open_element_attr(snapshot_xml_writer_t *w, const char *name,
                                    const char *attr_name, const char *attr_value)
{
    char *escaped_val = xml_escape_text(attr_value);
    writer_indent(w);
    g_string_append_printf(w->buffer, "<%s %s=\"%s\">\n", name, attr_name, escaped_val);
    g_free(escaped_val);

    w->stack_top++;
    if (w->stack_top < WRITER_MAX_DEPTH) {
        w->element_stack[w->stack_top] = g_strdup(name);
    }
    w->indent_level++;
}


void snapshot_xml_close_element(snapshot_xml_writer_t *w)
{
    if (w->stack_top < 0) return;

    w->indent_level--;
    writer_indent(w);
    g_string_append_printf(w->buffer, "</%s>\n", w->element_stack[w->stack_top]);

    g_free(w->element_stack[w->stack_top]);
    w->element_stack[w->stack_top] = NULL;
    w->stack_top--;
}


/**
 * Interní: zapíše jednořádkový element s hodnotou a type atributem
 */
static void writer_value_element(snapshot_xml_writer_t *w, const char *name,
                                  const char *type, const char *value)
{
    writer_indent(w);
    g_string_append_printf(w->buffer, "<%s type=\"%s\">%s</%s>\n",
                           name, type, value, name);
}


void snapshot_xml_write_string(snapshot_xml_writer_t *w, const char *name, const char *value)
{
    char *escaped = xml_escape_text(value);
    writer_value_element(w, name, "string", escaped);
    g_free(escaped);
}


void snapshot_xml_write_int(snapshot_xml_writer_t *w, const char *name, int value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    writer_value_element(w, name, "int", buf);
}


void snapshot_xml_write_uint(snapshot_xml_writer_t *w, const char *name, unsigned value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", value);
    writer_value_element(w, name, "uint", buf);
}


void snapshot_xml_write_hex8(snapshot_xml_writer_t *w, const char *name, uint8_t value)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%02X", value);
    writer_value_element(w, name, "hex8", buf);
}


void snapshot_xml_write_hex16(snapshot_xml_writer_t *w, const char *name, uint16_t value)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%04X", value);
    writer_value_element(w, name, "hex16", buf);
}


void snapshot_xml_write_hex32(snapshot_xml_writer_t *w, const char *name, uint32_t value)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%08X", value);
    writer_value_element(w, name, "hex32", buf);
}


void snapshot_xml_write_uint64(snapshot_xml_writer_t *w, const char *name, uint64_t value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%" PRIu64, value);
    writer_value_element(w, name, "uint64", buf);
}


void snapshot_xml_write_bool(snapshot_xml_writer_t *w, const char *name, bool value)
{
    writer_value_element(w, name, "bool", value ? "true" : "false");
}


char *snapshot_xml_writer_finish(snapshot_xml_writer_t *w)
{
    /* Zavřít všechny otevřené elementy */
    while (w->stack_top >= 0) {
        snapshot_xml_close_element(w);
    }

    char *result = g_string_free(w->buffer, FALSE);
    w->buffer = NULL;
    g_free(w);
    return result;
}


/* ========================================================================= */
/*                        XML Reader — DOM strom                             */
/* ========================================================================= */

/**
 * Uzel DOM stromu
 */
typedef struct xml_node {
    char *name;                   /* Název elementu */
    char *text;                   /* Textový obsah (NULL pokud nemá) */
    GHashTable *attrs;            /* Atributy: name → value (stringy, NULL pokud nemá) */
    GPtrArray *children;          /* Potomci (xml_node*) */
    struct xml_node *parent;      /* Rodičovský uzel */
} xml_node;


struct snapshot_xml_reader_t {
    xml_node *root;               /* Kořen DOM stromu */
    xml_node *current;            /* Aktuální pozice navigace */
};


/* Kontext pro GMarkup SAX parser */
typedef struct {
    xml_node *root;               /* Kořen stromu (virtuální) */
    xml_node *current;            /* Aktuální uzel při parsování */
} xml_parse_context;


/**
 * Vytvoření nového uzlu
 */
static xml_node *xml_node_new(const char *name, xml_node *parent)
{
    xml_node *node = g_new0(xml_node, 1);
    node->name = g_strdup(name);
    node->children = g_ptr_array_new();
    node->parent = parent;
    return node;
}


/**
 * Rekurzivní uvolnění uzlu a všech potomků
 */
static void xml_node_free(xml_node *node)
{
    if (!node) return;

    g_free(node->name);
    g_free(node->text);

    if (node->attrs) {
        g_hash_table_destroy(node->attrs);
    }

    for (guint i = 0; i < node->children->len; i++) {
        xml_node_free(g_ptr_array_index(node->children, i));
    }
    g_ptr_array_free(node->children, TRUE);

    g_free(node);
}


/**
 * Najít child element podle jména
 */
static xml_node *xml_node_find_child(xml_node *node, const char *name)
{
    if (!node || !name) return NULL;

    for (guint i = 0; i < node->children->len; i++) {
        xml_node *child = g_ptr_array_index(node->children, i);
        if (strcmp(child->name, name) == 0) {
            return child;
        }
    }
    return NULL;
}


/* ========================================================================= */
/*                      GMarkup SAX callbacky                                */
/* ========================================================================= */

static void gmarkup_start_element(GMarkupParseContext *ctx,
                                   const gchar *element_name,
                                   const gchar **attr_names,
                                   const gchar **attr_values,
                                   gpointer user_data,
                                   GError **error)
{
    (void)ctx;
    (void)error;
    xml_parse_context *pc = user_data;

    /* Vytvořit nový uzel a přidat do stromu */
    xml_node *node = xml_node_new(element_name, pc->current);
    g_ptr_array_add(pc->current->children, node);

    /* Uložit atributy */
    if (attr_names && attr_names[0]) {
        node->attrs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
        for (int i = 0; attr_names[i]; i++) {
            g_hash_table_insert(node->attrs,
                                g_strdup(attr_names[i]),
                                g_strdup(attr_values[i]));
        }
    }

    /* Přesunout se do nového uzlu */
    pc->current = node;
}


static void gmarkup_end_element(GMarkupParseContext *ctx,
                                 const gchar *element_name,
                                 gpointer user_data,
                                 GError **error)
{
    (void)ctx;
    (void)element_name;
    (void)error;
    xml_parse_context *pc = user_data;

    /* Vrátit se na rodiče */
    if (pc->current->parent) {
        pc->current = pc->current->parent;
    }
}


static void gmarkup_text(GMarkupParseContext *ctx,
                           const gchar *text,
                           gsize text_len,
                           gpointer user_data,
                           GError **error)
{
    (void)ctx;
    (void)error;
    xml_parse_context *pc = user_data;

    /* Přeskočit čistě bílé znaky */
    gboolean all_whitespace = TRUE;
    for (gsize i = 0; i < text_len; i++) {
        if (!g_ascii_isspace(text[i])) {
            all_whitespace = FALSE;
            break;
        }
    }
    if (all_whitespace) return;

    /* Uložit text do aktuálního uzlu */
    if (pc->current->text) {
        /* Přidat k existujícímu textu (pro případ fragmentace) */
        char *new_text = g_strconcat(pc->current->text, text, NULL);
        g_free(pc->current->text);
        pc->current->text = new_text;
    } else {
        pc->current->text = g_strndup(text, text_len);
    }
}


static const GMarkupParser gmarkup_parser = {
    .start_element = gmarkup_start_element,
    .end_element = gmarkup_end_element,
    .text = gmarkup_text,
    .passthrough = NULL,
    .error = NULL
};


/* ========================================================================= */
/*                        Reader — veřejné API                               */
/* ========================================================================= */

snapshot_xml_reader_t *snapshot_xml_reader_new(const char *xml_string)
{
    if (!xml_string) return NULL;

    /* Vytvořit virtuální kořenový uzel */
    xml_node *root = xml_node_new("__root__", NULL);

    /* Parsovat XML přes GMarkup */
    xml_parse_context pc = {
        .root = root,
        .current = root
    };

    GMarkupParseContext *ctx = g_markup_parse_context_new(
        &gmarkup_parser, 0, &pc, NULL);

    GError *error = NULL;
    if (!g_markup_parse_context_parse(ctx, xml_string, strlen(xml_string), &error)) {
        SNAP_ERR("xml", "XML parse error: %s", error->message);
        g_error_free(error);
        g_markup_parse_context_free(ctx);
        xml_node_free(root);
        return NULL;
    }

    if (!g_markup_parse_context_end_parse(ctx, &error)) {
        SNAP_ERR("xml", "parse finalization error: %s", error->message);
        g_error_free(error);
        g_markup_parse_context_free(ctx);
        xml_node_free(root);
        return NULL;
    }

    g_markup_parse_context_free(ctx);

    /* Vytvořit reader */
    snapshot_xml_reader_t *r = g_new0(snapshot_xml_reader_t, 1);
    r->root = root;
    r->current = root;

    return r;
}


void snapshot_xml_reader_free(snapshot_xml_reader_t *r)
{
    if (!r) return;
    xml_node_free(r->root);
    g_free(r);
}


bool snapshot_xml_enter_element(snapshot_xml_reader_t *r, const char *name)
{
    if (!r || !name) return false;

    xml_node *child = xml_node_find_child(r->current, name);
    if (!child) return false;

    r->current = child;
    return true;
}


void snapshot_xml_leave_element(snapshot_xml_reader_t *r)
{
    if (!r || !r->current || !r->current->parent) return;
    r->current = r->current->parent;
}


/**
 * Interní: najde child element a vrátí jeho textovou hodnotu
 */
static const char *reader_get_child_text(snapshot_xml_reader_t *r, const char *name)
{
    if (!r || !name) return NULL;

    xml_node *child = xml_node_find_child(r->current, name);
    if (!child || !child->text) return NULL;

    return child->text;
}


bool snapshot_xml_read_string(snapshot_xml_reader_t *r, const char *name, char **out_value)
{
    const char *text = reader_get_child_text(r, name);
    if (!text) return false;
    *out_value = g_strdup(text);
    return true;
}


bool snapshot_xml_read_int(snapshot_xml_reader_t *r, const char *name, int *out_value)
{
    const char *text = reader_get_child_text(r, name);
    if (!text) return false;

    char *end;
    long val = strtol(text, &end, 10);
    if (end == text) return false;

    *out_value = (int)val;
    return true;
}


bool snapshot_xml_read_uint(snapshot_xml_reader_t *r, const char *name, unsigned *out_value)
{
    const char *text = reader_get_child_text(r, name);
    if (!text) return false;

    char *end;
    unsigned long val = strtoul(text, &end, 10);
    if (end == text) return false;

    *out_value = (unsigned)val;
    return true;
}


bool snapshot_xml_read_hex8(snapshot_xml_reader_t *r, const char *name, uint8_t *out_value)
{
    const char *text = reader_get_child_text(r, name);
    if (!text) return false;

    char *end;
    unsigned long val = strtoul(text, &end, 16);
    if (end == text) return false;

    *out_value = (uint8_t)val;
    return true;
}


bool snapshot_xml_read_hex16(snapshot_xml_reader_t *r, const char *name, uint16_t *out_value)
{
    const char *text = reader_get_child_text(r, name);
    if (!text) return false;

    char *end;
    unsigned long val = strtoul(text, &end, 16);
    if (end == text) return false;

    *out_value = (uint16_t)val;
    return true;
}


bool snapshot_xml_read_hex32(snapshot_xml_reader_t *r, const char *name, uint32_t *out_value)
{
    const char *text = reader_get_child_text(r, name);
    if (!text) return false;

    char *end;
    unsigned long val = strtoul(text, &end, 16);
    if (end == text) return false;

    *out_value = (uint32_t)val;
    return true;
}


bool snapshot_xml_read_uint64(snapshot_xml_reader_t *r, const char *name, uint64_t *out_value)
{
    const char *text = reader_get_child_text(r, name);
    if (!text) return false;

    char *end;
    uint64_t val = strtoull(text, &end, 10);
    if (end == text) return false;

    *out_value = val;
    return true;
}


bool snapshot_xml_read_bool(snapshot_xml_reader_t *r, const char *name, bool *out_value)
{
    const char *text = reader_get_child_text(r, name);
    if (!text) return false;

    if (g_ascii_strcasecmp(text, "true") == 0 || strcmp(text, "1") == 0) {
        *out_value = true;
        return true;
    } else if (g_ascii_strcasecmp(text, "false") == 0 || strcmp(text, "0") == 0) {
        *out_value = false;
        return true;
    }

    return false;
}


bool snapshot_xml_read_attr(snapshot_xml_reader_t *r, const char *attr_name, char **out_value)
{
    if (!r || !r->current || !attr_name || !out_value) return false;

    if (!r->current->attrs) return false;

    const char *val = g_hash_table_lookup(r->current->attrs, attr_name);
    if (!val) return false;

    *out_value = g_strdup(val);
    return true;
}
