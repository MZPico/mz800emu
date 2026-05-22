/* 
 * File:   version_xml_parser.c
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 6. září 2018, 7:31
 * 
 * 
 * ----------------------------- License -------------------------------------
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * ---------------------------------------------------------------------------
 */


#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <glib.h>

#include "version_xml_parser.h"
#include "version_check.h"

static gboolean debug = FALSE;
static gboolean quiet_errors = TRUE;
static int debug_element_depth = 0;
static GString *debug_string;

static gboolean parse_error = FALSE;


static void debug_indent ( int extra ) {
    (void) extra;
    int i = 0;
    while ( i < debug_element_depth ) {
        g_string_append ( debug_string, "  " );
        ++i;
    };
}

static st_VERSION_REPORT *g_report = NULL;
static st_VERSION_BRANCH *g_ui_version_check_branch = NULL;


static void parser_start_element_handler ( GMarkupParseContext *context, const gchar *element_name, const gchar **attribute_names, const gchar **attribute_values, gpointer user_data, GError **error ) {
    (void) user_data;
    int i;

    gint line, col;
    g_markup_parse_context_get_position ( context, &line, &col );

    debug_indent ( 0 );
    g_string_append_printf ( debug_string, "START ELEMENT(%d) '%s' <%d, %d>\n", debug_element_depth, element_name, line, col );


    i = 0;
    while ( attribute_names[i] != NULL ) {
        debug_indent ( 1 );
        g_string_append_printf ( debug_string, "%s=\"%s\"\n", attribute_names[i], attribute_values[i] );
        ++i;
    };

    ++debug_element_depth;

    /* parsing */

    GSList *list = (GSList *) g_markup_parse_context_get_element_stack ( context );
    guint path_length = g_slist_length ( list );

    if ( ( path_length == 1 ) && ( 0 == strcasecmp ( element_name, "mz800emu_versions_report" ) ) ) {

        char *elm_version;
        if ( g_markup_collect_attributes ( element_name, attribute_names, attribute_values, error,
                                           G_MARKUP_COLLECT_STRING, "elm-version", &elm_version,
                                           G_MARKUP_COLLECT_INVALID ) ) {

            g_report = version_check_report_new ( atoi ( elm_version ) );
        };

        version_check_branch_free ( g_ui_version_check_branch );
        g_ui_version_check_branch = NULL;

    } else if ( ( path_length == 2 ) && ( g_report ) && ( g_report->elm_version == 1 ) && ( 0 == strcasecmp ( element_name, "branch" ) ) ) {

        version_check_branch_free ( g_ui_version_check_branch );
        g_ui_version_check_branch = NULL;

        char *elm_version;
        char *tag;
        char *version;
        char *revision;
        char *date;
        if ( g_markup_collect_attributes ( element_name, attribute_names, attribute_values, error,
                                           G_MARKUP_COLLECT_STRING, "elm-version", &elm_version,
                                           G_MARKUP_COLLECT_STRING, "tag", &tag,
                                           G_MARKUP_COLLECT_STRING, "version", &version,
                                           G_MARKUP_COLLECT_STRING, "revision", &revision,
                                           G_MARKUP_COLLECT_STRING, "date", &date,
                                           G_MARKUP_COLLECT_INVALID ) ) {

            g_ui_version_check_branch = version_check_branch_new (
                                                  atoi ( elm_version ),
                                                  tag,
                                                  atoi ( version ),
                                                  atoi ( revision ),
                                                  date
                                                  );
        };
    };
}


static void parser_end_element_handler ( GMarkupParseContext *context, const gchar *element_name, gpointer user_data, GError **error ) {
    (void) user_data;
    (void) error;
    --debug_element_depth;

    gint line, col;
    g_markup_parse_context_get_position ( context, &line, &col );

    debug_indent ( 0 );
    g_string_append_printf ( debug_string, "END '%s' <%d, %d>\n", element_name, line, col );

    /* parsing */

    GSList *list = (GSList *) g_markup_parse_context_get_element_stack ( context );
    guint path_length = g_slist_length ( list );

    if ( ( path_length == 2 ) && ( g_report ) && ( g_ui_version_check_branch ) && ( 0 == strcasecmp ( (char*) g_slist_nth_data ( list, 0 ), "branch" ) ) ) {
        version_check_report_add_branch ( g_report, g_ui_version_check_branch );
        version_check_branch_free ( g_ui_version_check_branch );
        g_ui_version_check_branch = NULL;
    };
}


static void parser_text_handler ( GMarkupParseContext *context, const gchar *text, gsize text_len, gpointer user_data, GError **error ) {
    (void) user_data;
    (void) error;
    debug_indent ( 0 );
    g_string_append_printf ( debug_string, "TEXT '%.*s'\n", (int) text_len, text );

    /* parsing */

    GSList *list = (GSList *) g_markup_parse_context_get_element_stack ( context );
    guint path_length = g_slist_length ( list );

    if ( ( path_length == 3 ) && ( g_report ) && ( g_report->elm_version == 1 ) && ( g_ui_version_check_branch ) && ( g_ui_version_check_branch->elm_version == 1 ) ) {
        if ( 0 == strcasecmp ( (char*) g_slist_nth_data ( list, 0 ), "msg" ) ) {
            version_check_branch_set_msg ( g_ui_version_check_branch, text );
        };
    };
}


static void parser_passthrough_handler ( GMarkupParseContext *context, const gchar *passthrough_text, gsize text_len, gpointer user_data, GError **error ) {
    (void) user_data;
    (void) context;
    (void) error;
    debug_indent ( 0 );
    g_string_append_printf ( debug_string, "PASS '%.*s'\n", (int) text_len, passthrough_text );
}


static void parser_error_handler ( GMarkupParseContext *context, GError *error, gpointer user_data ) {
    (void) context;
    (void) user_data;
    (void) error;
    g_string_append_printf ( debug_string, "ERROR %s\n", error->message );
    if ( !quiet_errors ) {
        fprintf ( stderr, "VERSION_CHECK_XML_PARSER: %s\n", error->message );
    };
    parse_error = TRUE;
}

static const GMarkupParser parser = {
                                     parser_start_element_handler,
                                     parser_end_element_handler,
                                     parser_text_handler,
                                     parser_passthrough_handler,
                                     parser_error_handler
};


static void parse_xml_document ( gchar *xmldocument, gsize length ) {

    debug_element_depth = 0;
    GMarkupParseFlags flags = 0;

    GMarkupParseContext *context = g_markup_parse_context_new ( &parser, flags, NULL, NULL );
    assert ( g_markup_parse_context_get_user_data ( context ) == NULL );

    gint line, col;
    g_markup_parse_context_get_position ( context, &line, &col );
    assert ( line == 1 && col == 1 );
    if ( ( line != 1 && col != 1 ) ) {
        parse_error = TRUE;
        return;
    };

    if ( !g_markup_parse_context_parse ( context, xmldocument, length, NULL ) ) {
        g_markup_parse_context_free ( context );
        parse_error = TRUE;
        return;
    };

    if ( !g_markup_parse_context_end_parse ( context, NULL ) ) {
        g_markup_parse_context_free ( context );
        parse_error = TRUE;
        return;
    };

    g_markup_parse_context_free ( context );
}

//#define VERSION_XML_PARSER_TESTFILE "test.xml"


st_VERSION_REPORT* version_xml_document_parse ( gchar *xmldocument, gsize length, gboolean quiet ) {

    quiet_errors = quiet;

    g_report = NULL;

    version_check_branch_free ( g_ui_version_check_branch );
    g_ui_version_check_branch = NULL;

    parse_error = FALSE;
    debug_string = g_string_sized_new ( 0 );

#ifdef VERSION_XML_PARSER_TESTFILE
    const gchar *filename = VERSION_XML_PARSER_TESTFILE;
    GError *error = NULL;
    if ( !g_file_get_contents ( filename,
                                &xmldocument,
                                &length,
                                &error ) ) {
        fprintf ( stderr, "%s\n", error->message );
        g_error_free ( error );
        return NULL;
    }
#endif

    parse_xml_document ( xmldocument, length );

#ifdef VERSION_XML_PARSER_TESTFILE
    g_free ( xmldocument );
#endif

    if ( debug ) {
        g_print ( "%s", debug_string->str );
    };

    g_string_free ( debug_string, TRUE );

    if ( parse_error ) {
        version_check_report_free ( g_report );
        g_report = NULL;
    };

    return g_report;
}
