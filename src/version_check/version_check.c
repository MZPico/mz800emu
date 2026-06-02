/* 
 * File:   version_check.c
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 28. srpna 2018, 7:46
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

#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

//#define VERCHECK_USE_LIBSOUP
#define VERCHECK_USE_LIBCURL

#ifdef VERCHECK_USE_LIBSOUP
#include <libsoup/soup.h>
#endif

#ifdef VERCHECK_USE_LIBCURL
#include <curl/curl.h>
#endif /* VERCHECK_USE_LIBCURL */

#include "ui-imgui/version_check/imgui_version_check.h"
#include "libs/sdlapp/sdlapp_options.h"
#include "ui-imgui/bootstrap/myimgui.h"
#define ui_version_check_show_report_window(a) imgui_version_check_report_done ( a )
#define ui_version_check_show_setup_window() {g_gui->showVersionCheckSetupWindow = true;}
#include "baseui/baseui.h"
#define ui_show_error(a,...) baseui_show_message ( TRUE, a, ##__VA_ARGS__ )
#define ui_show_warning(a,...) baseui_show_message ( FALSE, a, ##__VA_ARGS__ )

#include "baseui/baseui_tools.h"
#include "cfgmain.h"
#include "build_revision/build_revision.h"

#include "version_check.h"
#include "version_xml_parser.h"


typedef struct st_VERSION_CHECK {
    CFGELM *elm_period;
    CFGELM *elm_last_check;
    CFGELM *elm_startup_counter;
    CFGELM *elm_ignore_major_tag;
    CFGELM *elm_ignore_major_version;
    CFGELM *elm_ignore_major_revision;
    CFGELM *elm_ignore_running_tag;
    CFGELM *elm_ignore_running_version;
    CFGELM *elm_ignore_running_revision;
    CFGELM *elm_proxy_settings;
    CFGELM *elm_proxy_server;

    GThread *thread;

#ifdef VERCHECK_USE_LIBSOUP
    SoupSession *session;
#endif /* VERCHECK_USE_LIBSOUP */

    gboolean debug;
    gboolean quiet;
    gboolean manual;
    GString *reply;
    char *formdata;

    st_VERSION_REPORT *report;
} st_VERSION_CHECK;

st_VERSION_CHECK g_version_check;

gboolean g_version_check_thread_done = FALSE;


GString* version_check_create_version_string ( guint32 version ) {
    GString *vstring = g_string_sized_new ( 0 );
    g_string_append_printf ( vstring, "%d.%d.%d", ( version >> 24 ) & 0xff, ( version >> 16 ) & 0xff, ( version >> 8 ) & 0xff );
    if ( version & 0xff ) {
        g_string_append_printf ( vstring, ".%d", version & 0xff );
    };
    return vstring;
}


st_VERSION_BRANCH* version_check_report_search_branch ( st_VERSION_REPORT *report, gchar *tag ) {
    (void) report;
    int i;
    for ( i = 0; (unsigned)i < g_version_check.report->count_branch; i++ ) {
        st_VERSION_BRANCH *branch = g_version_check.report->branch[i];
        if ( 0 == strcasecmp ( branch->tag->str, tag ) ) {
            return branch;
        };
    };
    return NULL;
}


st_VERSION_REPORT* version_check_report_new ( guint32 elm_version ) {
    st_VERSION_REPORT *report = (st_VERSION_REPORT*) baseui_tools_mem_alloc0 ( sizeof ( st_VERSION_REPORT ) );
    report->elm_version = elm_version;
    return report;
}


st_VERSION_BRANCH* version_check_branch_new ( guint32 elm_version, gchar *tag, guint32 version, guint32 revision, gchar *date ) {
    st_VERSION_BRANCH *branch = (st_VERSION_BRANCH*) baseui_tools_mem_alloc0 ( sizeof ( st_VERSION_BRANCH ) );
    branch->elm_version = elm_version;
    branch->tag = g_string_new ( tag );
    branch->version = version;
    branch->revision = revision;
    branch->date = g_string_new ( date );
    return branch;
}


void version_check_branch_free ( st_VERSION_BRANCH *branch ) {
    if ( !branch ) return;
    g_string_free ( branch->tag, TRUE );
    g_string_free ( branch->date, TRUE );
    if ( branch->msg ) g_string_free ( branch->msg, TRUE );
    g_free ( branch );
    return;
}


void version_check_report_free ( st_VERSION_REPORT *report ) {
    if ( !report ) return;
    int i;
    for ( i = 0; (unsigned)i < report->count_branch; i++ ) {
        version_check_branch_free ( report->branch[i] );
    };
    if ( report->branch ) g_free ( report->branch );
    g_free ( report );
}


void version_check_branch_set_msg ( st_VERSION_BRANCH *branch, const gchar *msg ) {
    if ( branch->msg ) g_string_free ( branch->msg, TRUE );
    branch->msg = g_string_new ( msg );
}


st_VERSION_BRANCH* version_check_copy_branch ( st_VERSION_BRANCH *branch ) {
    st_VERSION_BRANCH *new_branch = version_check_branch_new ( branch->elm_version, branch->tag->str, branch->version, branch->revision, branch->date->str );
    if ( branch->msg ) version_check_branch_set_msg ( branch, branch->msg->str );
    return new_branch;
}


void version_check_report_add_branch ( st_VERSION_REPORT *report, st_VERSION_BRANCH *branch ) {

    gsize size = ( report->count_branch + 1 ) * sizeof ( report->branch );

    if ( !report->branch ) {
        report->branch = baseui_tools_mem_alloc ( size );
    } else {
        report->branch = baseui_tools_mem_realloc ( report->branch, size );
    };

    st_VERSION_BRANCH *new_branch = version_check_branch_new ( branch->elm_version, branch->tag->str, branch->version, branch->revision, branch->date->str );
    if ( branch->msg ) version_check_branch_set_msg ( branch, branch->msg->str );
    report->branch[report->count_branch] = new_branch;

    report->count_branch++;
}


uint32_t version_check_get_unix_days ( void ) {
    GTimeZone *tz = g_time_zone_new_local ( );
    GDateTime *datetime = g_date_time_new_now ( tz );
    uint64_t unixtime = g_date_time_to_unix ( datetime );
    g_time_zone_unref ( tz );
    g_date_time_unref ( datetime );
    uint64_t unix_days = unixtime / ( 3600 * 24 );
    return (uint32_t) unix_days;
}


static uint32_t version_check_get_startup_counter ( void ) {
    uint32_t startup_counter = cfgelement_get_unsigned_value ( g_version_check.elm_startup_counter );
    return startup_counter;
}


static void version_check_increment_startup_counter ( void ) {
    uint32_t startup_counter = version_check_get_startup_counter ( );
    cfgelement_set_unsigned_value ( g_version_check.elm_startup_counter, startup_counter + 1 );
}


static void version_check_reset_startup_counter ( void ) {
    cfgelement_set_unsigned_value ( g_version_check.elm_startup_counter, 0 );
}


en_VERSION_CHECK_PERIOD version_check_get_period ( void ) {
    return cfgelement_get_keyword_value ( g_version_check.elm_period );
}


void version_check_set_period ( en_VERSION_CHECK_PERIOD period ) {
    cfgelement_set_keyword_value ( g_version_check.elm_period, period );
}


uint32_t version_check_get_last_check ( void ) {
    return cfgelement_get_unsigned_value ( g_version_check.elm_last_check );
}


void version_check_update_last_check ( void ) {
    cfgelement_set_unsigned_value ( g_version_check.elm_last_check, version_check_get_unix_days ( ) );
}


void version_check_set_ignored ( char *tag, uint32_t version, uint32_t revision ) {
    if ( ( tag[0] == 0x00 ) || ( 0 == strcasecmp ( tag, VERSION_CHECK_MAJOR_BRANCH ) ) ) {
        cfgelement_set_text_value ( g_version_check.elm_ignore_major_tag, tag );
        cfgelement_set_unsigned_value ( g_version_check.elm_ignore_major_version, version );
        cfgelement_set_unsigned_value ( g_version_check.elm_ignore_major_revision, revision );
    };
    if ( ( tag[0] == 0x00 ) || ( 0 == strcasecmp ( tag, CFGMAIN_EMULATOR_VERSION_TAG ) ) ) {
        cfgelement_set_text_value ( g_version_check.elm_ignore_running_tag, tag );
        cfgelement_set_unsigned_value ( g_version_check.elm_ignore_running_version, version );
        cfgelement_set_unsigned_value ( g_version_check.elm_ignore_running_revision, revision );
    };
}


en_VERSION_CHECK_PROXY version_check_get_proxy_settings ( void ) {
    return cfgelement_get_keyword_value ( g_version_check.elm_proxy_settings );
}


void version_check_set_proxy_settings ( en_VERSION_CHECK_PROXY use_proxy ) {
    cfgelement_set_keyword_value ( g_version_check.elm_proxy_settings, use_proxy );
}


const char* version_check_get_proxy_server ( void ) {
    return cfgelement_get_text_value ( g_version_check.elm_proxy_server );
}


void version_check_set_proxy_server ( const char *proxy_server ) {
    cfgelement_set_text_value ( g_version_check.elm_proxy_server, proxy_server );
}

#ifdef VERCHECK_USE_LIBSOUP
static void version_check_get_url_libsoup ( const char *url ) {

    const char *name;
    SoupMessage *msg;
    const char *header;

    msg = soup_message_new ( "POST", url );

    soup_message_set_request ( msg, "application/x-www-form-urlencoded",
                               SOUP_MEMORY_COPY, g_version_check.formdata, strlen ( g_version_check.formdata ) );

    soup_message_set_flags ( msg, SOUP_MESSAGE_NO_REDIRECT );

    soup_session_send_message ( g_version_check.session, msg );

    name = soup_message_get_uri ( msg )->path;

    if ( !g_version_check.debug ) {
        if ( msg->status_code == SOUP_STATUS_SSL_FAILED ) {
            GTlsCertificateFlags flags;

            if ( soup_message_get_https_status ( msg, NULL, &flags ) ) {
                g_print ( "%s():%d - %s: %d %s (0x%x)\n", __func__, __LINE__, name, msg->status_code, msg->reason_phrase, flags );
            } else {
                g_print ( "%s():%d - %s: %d %s (no handshake status)\n", __func__, __LINE__, name, msg->status_code, msg->reason_phrase );
            };
        } else if ( ( !g_version_check.quiet ) && SOUP_STATUS_IS_TRANSPORT_ERROR ( msg->status_code ) ) {
            g_print ( "%s():%d - %s: %d %s\n", __func__, __LINE__, name, msg->status_code, msg->reason_phrase );
        };
    };

    if ( SOUP_STATUS_IS_REDIRECTION ( msg->status_code ) ) {
        header = soup_message_headers_get_one ( msg->response_headers, "Location" );
        if ( header ) {
            SoupURI *uri;
            char *uri_string;

            if ( !g_version_check.debug && !g_version_check.quiet ) {
                g_print ( "%s():%d --> %s\n", __func__, __LINE__, header );
            };

            uri = soup_uri_new_with_base ( soup_message_get_uri ( msg ), header );
            uri_string = soup_uri_to_string ( uri, FALSE );
            version_check_get_url_libsoup ( uri_string );
            g_free ( uri_string );
            soup_uri_free ( uri );
        }
    } else if ( SOUP_STATUS_IS_SUCCESSFUL ( msg->status_code ) ) {

        if ( msg->response_body->length ) {
            g_version_check.reply = g_string_new ( msg->response_body->data );
        };
    };

    g_object_unref ( msg );
}
#endif /* VERCHECK_USE_LIBSOUP */


#ifdef VERCHECK_USE_LIBCURL
/**
 * @brief Callback funkce pro příjem dat z CURLu
 */
static size_t version_check_curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    GString **response = (GString **)userp;

    if (!*response)
        *response = g_string_new_len(contents, realsize);
    else
        g_string_append_len(*response, contents, realsize);

    return realsize;
}

/**
 * @brief Nastaví CA cert, pokud existuje home_dir/certs/cacert.pem
 */
static void set_cacert_if_available(CURL *curl) {
    char *cert_path = sdlapp_paths_resolve_home(g_sdlapp->paths, "certs/cacert.pem");

    if (g_file_test(cert_path, G_FILE_TEST_EXISTS)) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, cert_path);
        if (g_version_check.debug) {
            g_print("Using CA cert from: %s\n", cert_path);
        }
    } else {
        if (g_version_check.debug) {
            g_print("CA cert file '%s' not found, using default\n", cert_path);
        }
    }
    g_free(cert_path);
}


/**
 * @brief Provádí HTTP POST pomocí CURL
 */
static void version_check_get_url_curl(const char *url, const char *formdata, const char *proxy) {
    CURL *curl = curl_easy_init();
    CURLcode res;

    if (!curl) {
        g_printerr("%s():%d - Failed to initialize CURL\n", __func__, __LINE__);
        return;
    }

    //curl_easy_setopt(curl, CURLOPT_CAINFO, "certs/cacert.pem");
    set_cacert_if_available(curl);
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, formdata);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "post");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""); // pro gzip

    if (proxy && strlen(proxy)) {
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy);
    }

    g_version_check.reply = NULL;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, version_check_curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &g_version_check.reply);

    if (g_version_check.debug) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        g_printerr("%s():%d - Request failed: %s\n", __func__, __LINE__, curl_easy_strerror(res));
    } else {
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        if (!g_version_check.quiet) {
            g_print("%s():%d - HTTP %ld OK\n", __func__, __LINE__, response_code);
        }
    }

    curl_easy_cleanup(curl);
}
#endif /* VERCHECK_USE_LIBCURL */

static void version_check_add_uri_variable ( char **formdata, char *var_name, char type, const void *value_p ) {

    int len = strlen ( var_name ) + 1; // + '='
    char *s_value = NULL;

    if ( type == 's' ) {
        s_value = g_uri_escape_string ( (char*) value_p, NULL, FALSE );
        len += strlen ( s_value );
    } else if ( type == 'd' ) {
        len += 10; //uint32
        s_value = (char*) baseui_tools_mem_alloc0 ( 10 + 1 );
        const uint32_t *d = value_p;
        snprintf ( s_value, ( 10 + 1 ), "%d", *d );
    } else {
        fprintf ( stderr, "%s():%d - Unknown format type '%c'\n", __func__, __LINE__, type );
        return;
    };

    len++;

    char *var_data = (char*) baseui_tools_mem_alloc0 ( len );
    snprintf ( var_data, len, "%s=%s", var_name, s_value );
    g_free ( s_value );

    if ( !*formdata ) {
        *formdata = var_data;
    } else {
        len += strlen ( *formdata ) + 1; // + '&'
        *formdata = (char*) baseui_tools_mem_realloc ( *formdata, len );
        strncat ( *formdata, "&", len );
        strncat ( *formdata, var_data, len );
        g_free ( var_data );
    };
}


static char* version_check_create_post_data ( void ) {

    char *formdata = NULL;

    uint32_t d = cfgmain_get_version_uint32 ( );
    version_check_add_uri_variable ( &formdata, "version", 'd', &d );

    version_check_add_uri_variable ( &formdata, "tag", 's', CFGMAIN_EMULATOR_VERSION_TAG );
    version_check_add_uri_variable ( &formdata, "platform", 's', CFGMAIN_PLATFORM );

    d = build_revision_get_int ( );
    version_check_add_uri_variable ( &formdata, "revision", 'd', &d );

    version_check_add_uri_variable ( &formdata, "buildtime", 's', cfgmain_get_build_datetime ( ) );

    d = version_check_get_period ( );
    version_check_add_uri_variable ( &formdata, "period", 'd', &d );

    d = version_check_get_last_check ( );
    version_check_add_uri_variable ( &formdata, "last_check", 'd', &d );

    d = version_check_get_startup_counter ( );
    version_check_add_uri_variable ( &formdata, "starts", 'd', &d );

    version_check_add_uri_variable ( &formdata, "manual", 'd', &g_version_check.manual );

    return formdata;
}

#ifdef VERCHECK_USE_LIBSOUP
static void version_check_make_http_test_libsoup ( void ) {

    const char *url = VERSION_CHECK_URL;
    const char *proxy = NULL;
    SoupURI *proxy_uri;
    SoupURI *parsed;
    SoupLogger *logger = NULL;

    if ( g_version_check.reply ) g_string_free ( g_version_check.reply, TRUE );
    g_version_check.reply = NULL;

    parsed = soup_uri_new ( url );
    if ( !parsed ) {
        g_printerr ( "%s():%d - Could not parse '%s' as a URL\n", __func__, __LINE__, url );
        return;
    }
    soup_uri_free ( parsed );

    g_version_check.session = soup_session_new_with_options (
                                                              SOUP_SESSION_ADD_FEATURE_BY_TYPE, SOUP_TYPE_CONTENT_DECODER,
                                                              SOUP_SESSION_ADD_FEATURE_BY_TYPE, SOUP_TYPE_COOKIE_JAR,
                                                              SOUP_SESSION_USER_AGENT, "post ",
                                                              SOUP_SESSION_ACCEPT_LANGUAGE_AUTO, TRUE,
                                                              NULL );

    if ( g_version_check.debug ) {
        logger = soup_logger_new ( SOUP_LOGGER_LOG_BODY, -1 );
        soup_session_add_feature ( g_version_check.session, SOUP_SESSION_FEATURE ( logger ) );
        g_object_unref ( logger );
    }

    en_VERSION_CHECK_PROXY use_proxy = version_check_get_proxy_settings ( );

    if ( use_proxy == VERSION_CHECK_PROXY_SYSTEM ) {
        char *http_proxy, *https_proxy;
        http_proxy = getenv ( "HTTP_PROXY" );
        https_proxy = getenv ( "HTTPS_PROXY" );
        proxy = ( https_proxy ) ? https_proxy : http_proxy;

        if ( proxy ) {
            printf ( "VERSION_CHECK: using system HTTP proxy %s\n", proxy );
        } else {
            printf ( "VERSION_CHECK: HTTP proxy is not defined in enviroment\n" );
        };
    } else if ( use_proxy == VERSION_CHECK_PROXY_MANUAL ) {
        char *manual_proxy = (char*) version_check_get_proxy_server ( );
        if ( strlen ( manual_proxy ) ) {
            proxy = manual_proxy;
            printf ( "VERSION_CHECK: using manual HTTP proxy %s\n", proxy );
        } else {
            printf ( "VERSION_CHECK: manual HTTP proxy is not set\n" );
        };
    }


    if ( proxy ) {
        proxy_uri = soup_uri_new ( proxy );
        if ( !proxy_uri ) {
            g_printerr ( "%s():%d - Could not parse proxy '%s' as URI\n", __func__, __LINE__, proxy );
            g_object_unref ( g_version_check.session );
            return;
        };
        g_object_set ( G_OBJECT ( g_version_check.session ), SOUP_SESSION_PROXY_URI, proxy_uri, NULL );
        soup_uri_free ( proxy_uri );
    };

    g_version_check.formdata = version_check_create_post_data ( );

    version_check_get_url_libsoup ( url );

    g_free ( g_version_check.formdata );
    g_version_check.formdata = NULL;

    g_object_unref ( g_version_check.session );
    g_version_check.session = NULL;
}
#endif /* VERCHECK_USE_LIBSOUP */


#ifdef VERCHECK_USE_LIBCURL
/**
 * @brief Příprava a volání CURL verze kontrolní HTTP komunikace
 */
static void version_check_make_http_test_curl(void) {
    const char *url = VERSION_CHECK_URL;
    const char *proxy = NULL;

    if (g_version_check.reply) {
        g_string_free(g_version_check.reply, TRUE);
        g_version_check.reply = NULL;
    }

    // detekce proxy
    en_VERSION_CHECK_PROXY use_proxy = version_check_get_proxy_settings();
    if (use_proxy == VERSION_CHECK_PROXY_SYSTEM) {
        char *http_proxy = getenv("HTTP_PROXY");
        char *https_proxy = getenv("HTTPS_PROXY");
        proxy = https_proxy ? https_proxy : http_proxy;

        if (proxy)
            printf("VERSION_CHECK: using system HTTP proxy %s\n", proxy);
        else
            printf("VERSION_CHECK: HTTP proxy is not defined in environment\n");
    } else if (use_proxy == VERSION_CHECK_PROXY_MANUAL) {
        proxy = version_check_get_proxy_server();
        if (strlen(proxy))
            printf("VERSION_CHECK: using manual HTTP proxy %s\n", proxy);
        else
            printf("VERSION_CHECK: manual HTTP proxy is not set\n");
    }

    g_version_check.formdata = version_check_create_post_data();

    version_check_get_url_curl(url, g_version_check.formdata, proxy);

    g_free(g_version_check.formdata);
    g_version_check.formdata = NULL;
}
#endif /* VERCHECK_USE_LIBCURL */


static gpointer version_check_run_test_thread ( gpointer data ) {
    (void) data;

#ifdef VERCHECK_USE_LIBSOUP
    version_check_make_http_test_libsoup ( );
#endif /* VERCHECK_USE_LIBSOUP */

#ifdef VERCHECK_USE_LIBCURL
    version_check_make_http_test_curl ( );
#endif /* VERCHECK_USE_LIBCURL */


    if ( g_version_check.reply ) {
#if 0
        g_print ( "\n\nVersion check:\n\n%s\n\n", g_version_check.reply->str );
#endif
        g_version_check.report = version_xml_document_parse ( g_version_check.reply->str, g_version_check.reply->len, g_version_check.quiet );
        g_string_free ( g_version_check.reply, TRUE );
        g_version_check.reply = NULL;
    };

    g_version_check_thread_done = TRUE;
    return NULL;
}


void version_check_print_available_ignored ( st_VERSION_BRANCH *branch ) {
    GString *verstr = version_check_create_version_string ( branch->version );
    printf ( "VERSION_CHECK: available version %s %s, rev: %d is ignored\n", branch->tag->str, verstr->str, branch->revision );
    g_string_free ( verstr, TRUE );
}


void version_check_parse_thread_response ( void ) {

    gboolean err = TRUE;

    if ( g_version_check.report ) {

        version_check_reset_startup_counter ( );

        if ( g_version_check.manual ) {
            printf ( "\nVERSION CHECK REPORT IS DONE:\n\n" );

            int i;
            for ( i = 0; (unsigned)i < g_version_check.report->count_branch; i++ ) {
                st_VERSION_BRANCH *branch = g_version_check.report->branch[i];
                GString *vstring = version_check_create_version_string ( branch->version );
                printf ( "%s, ver: %s, rev: %d, date: %s\n", branch->tag->str, vstring->str, branch->revision, branch->date->str );
                g_string_free ( vstring, TRUE );
            };
            printf ( "\n" );
        };

        st_VERSION_BRANCH *major_branch = version_check_report_search_branch ( g_version_check.report, VERSION_CHECK_MAJOR_BRANCH );
        st_VERSION_BRANCH *running_branch = version_check_report_search_branch ( g_version_check.report, CFGMAIN_EMULATOR_VERSION_TAG );

        if ( major_branch ) {
            err = FALSE;

            uint32_t version = cfgmain_get_version_uint32 ( );
            uint32_t revision = build_revision_get_int ( );

            if ( g_version_check.manual ) version_check_set_ignored ( "", 0, 0 );

            const char *ignored_major_tag = cfgelement_get_text_value ( g_version_check.elm_ignore_major_tag );
            uint32_t ignored_major_version = cfgelement_get_unsigned_value ( g_version_check.elm_ignore_major_version );
            uint32_t ignored_major_revision = cfgelement_get_unsigned_value ( g_version_check.elm_ignore_major_revision );

            const char *ignored_running_tag = cfgelement_get_text_value ( g_version_check.elm_ignore_running_tag );
            uint32_t ignored_running_version = cfgelement_get_unsigned_value ( g_version_check.elm_ignore_running_version );
            uint32_t ignored_running_revision = cfgelement_get_unsigned_value ( g_version_check.elm_ignore_running_revision );

            gboolean fl_ignore_major = ( ( 0 == strcasecmp ( ignored_major_tag, major_branch->tag->str ) ) && ( major_branch->version == ignored_major_version ) && ( major_branch->revision == ignored_major_revision ) ) ? TRUE : FALSE;
            gboolean fl_ignore_running = ( ( 0 == strcasecmp ( ignored_running_tag, running_branch->tag->str ) ) && ( running_branch->version == ignored_running_version ) && ( running_branch->revision == ignored_running_revision ) ) ? TRUE : FALSE;

            gboolean major_update_is_available = FALSE;
            gboolean running_update_is_available = FALSE;
            // st_VERSION_BRANCH *branch = NULL;

            if ( ( major_branch->version > version ) || ( major_branch->revision > revision ) ) {
                major_update_is_available = TRUE;
            };

            if ( ( running_branch ) && ( major_branch != running_branch ) ) {
                if ( ( running_branch->version > version ) || ( running_branch->revision > revision ) ) {
                    running_update_is_available = TRUE;
                };
            };

            if ( major_update_is_available || running_update_is_available ) {

                if ( fl_ignore_major && major_update_is_available ) {
                    version_check_print_available_ignored ( major_branch );
                };

                if ( fl_ignore_running && running_update_is_available ) {
                    version_check_print_available_ignored ( running_branch );
                };

                if ( !fl_ignore_major && major_update_is_available ) {
                    ui_version_check_show_report_window ( major_branch );
                } else if ( !fl_ignore_running && running_update_is_available ) {
                    ui_version_check_show_report_window ( running_branch );
                } else {
                    version_check_update_last_check ( );
                };

            } else {
                if ( g_version_check.manual ) {
                    st_VERSION_BRANCH *branch = ( running_branch ) ? running_branch : major_branch;
                    ui_version_check_show_report_window ( branch );
                } else {
                    version_check_update_last_check ( );
                };
            };

        };

        version_check_report_free ( g_version_check.report );
        g_version_check.report = NULL;

    };

    if ( err ) {
        /* Fallback URL po migraci ze SourceForge na GitHub - směrujeme
         * uživatele na release stránku v novém repozitáři. */
        printf ( "\nVERSION_CHECK:\n\tNo valid reply from %s\n", VERSION_CHECK_URL );
        printf ( "\tPlease check latest version manually on project homepage https://github.com/michalhucik/mz800emu/releases\n\n" );
        if ( !g_version_check.quiet ) {
            ui_show_warning ( "Module VERSION_CHECK:\n\nNo valid reply from %s\n\nPlease check latest version manually on project homepage:\n\nhttps://github.com/michalhucik/mz800emu/releases\n", VERSION_CHECK_URL );
        };
    };

    g_thread_unref ( g_version_check.thread );
    g_version_check.thread = NULL;
    g_version_check_thread_done = FALSE;
}


void version_check_run_test ( gboolean manual ) {

    if ( g_version_check.thread ) {
        ui_show_warning ( "Another process of version_check_test is already running!\n" );
        return;
    };

    g_version_check.manual = manual;

    if ( manual ) {
        g_version_check.quiet = FALSE;
    } else {
        g_version_check.quiet = TRUE;
    };

    printf ( "VERSION_CHECK: Scan URL - %s\n", VERSION_CHECK_URL );

    GError *error;
    g_version_check.thread = g_thread_try_new ( "version_check_test", version_check_run_test_thread, NULL, &error );

    if ( !g_version_check.thread ) {
        ui_show_error ( "%s():%d - %s\n", __func__, __LINE__, error->message );
    };
}


void version_check_init ( void ) {

    g_version_check_thread_done = FALSE;
    g_version_check.thread = NULL;

#ifdef VERCHECK_USE_LIBSOUP
    g_version_check.session = NULL;
#endif /* VERCHECK_USE_LIBSOUP */

    g_version_check.debug = FALSE;
    g_version_check.quiet = FALSE;
    g_version_check.manual = FALSE;
    g_version_check.reply = NULL;
    g_version_check.formdata = NULL;
    g_version_check.report = NULL;

    CFGMOD *cmod = cfgroot_register_new_module ( g_cfgmain, "VERSION_CHECK" );

    g_version_check.elm_period = cfgmodule_register_new_element ( cmod, "check_period", CFGENTYPE_KEYWORD, VERSION_CHECK_PERIOD_UNKNOWN,
                                                                  VERSION_CHECK_PERIOD_UNKNOWN, "UNKNOWN",
                                                                  VERSION_CHECK_PERIOD_NEVER, "NEVER",
                                                                  VERSION_CHECK_PERIOD_DAILY, "DAILY",
                                                                  VERSION_CHECK_PERIOD_WEEKLY, "WEEKLY",
                                                                  VERSION_CHECK_PERIOD_MONTHLY, "MONTHLY",
                                                                  VERSION_CHECK_PERIOD_MONTHLY, "SOMETIMES",
                                                                  -1 );

    g_version_check.elm_last_check = cfgmodule_register_new_element ( cmod, "last_check", CFGENTYPE_UNSIGNED, 0, 0, 0xffffffff );

    g_version_check.elm_startup_counter = cfgmodule_register_new_element ( cmod, "startup_counter", CFGENTYPE_UNSIGNED, 0, 0, 0xffffffff );

    g_version_check.elm_ignore_major_tag = cfgmodule_register_new_element ( cmod, "ignore_major_tag", CFGENTYPE_TEXT, "" );
    g_version_check.elm_ignore_major_version = cfgmodule_register_new_element ( cmod, "ignore_major_version", CFGENTYPE_UNSIGNED, 0, 0, 0xffffffff );
    g_version_check.elm_ignore_major_revision = cfgmodule_register_new_element ( cmod, "ignore_major_revision", CFGENTYPE_UNSIGNED, 0, 0, 0xffffffff );

    g_version_check.elm_ignore_running_tag = cfgmodule_register_new_element ( cmod, "ignore_running_tag", CFGENTYPE_TEXT, "" );
    g_version_check.elm_ignore_running_version = cfgmodule_register_new_element ( cmod, "ignore_running_version", CFGENTYPE_UNSIGNED, 0, 0, 0xffffffff );
    g_version_check.elm_ignore_running_revision = cfgmodule_register_new_element ( cmod, "ignore_running_revision", CFGENTYPE_UNSIGNED, 0, 0, 0xffffffff );


    g_version_check.elm_proxy_settings = cfgmodule_register_new_element ( cmod, "use_proxy", CFGENTYPE_KEYWORD, VERSION_CHECK_PROXY_SYSTEM,
                                                                          VERSION_CHECK_PROXY_NONE, "NONE",
                                                                          VERSION_CHECK_PROXY_SYSTEM, "SYSTEM",
                                                                          VERSION_CHECK_PROXY_MANUAL, "MANUAL",
                                                                          -1 );

    g_version_check.elm_proxy_server = cfgmodule_register_new_element ( cmod, "proxy_server", CFGENTYPE_TEXT, "" );

    cfgmodule_parse ( cmod );

    version_check_increment_startup_counter ( );
    en_VERSION_CHECK_PERIOD period = version_check_get_period ( );

    if ( period == VERSION_CHECK_PERIOD_UNKNOWN ) {
        /* First-run scénář: setup okno se otevře aby uživatel zvolil
         * frekvenci. CLI flag --no-first-run-windows to potlačí (= headless
         * / scripted launch nechá period UNKNOWN, příští spuštění zase
         * nabídne setup). */
        if ( !sdlapp_option_present ( "--no-first-run-windows" ) ) {
            ui_version_check_show_setup_window ( );
        };
    } else {
        if ( period > VERSION_CHECK_PERIOD_NEVER ) {
            uint32_t last_check = version_check_get_last_check ( );
            uint32_t unix_days = version_check_get_unix_days ( );
            //g_print ( "VERSION_CHECK: last_check = %d, unix_days = %d\n", last_check, unix_days );
            if ( ( last_check == 0 ) || ( ( unix_days - last_check ) >= period ) ) {
                version_check_run_test ( FALSE );
            };
        };
    };
}


void version_check_exit ( void ) {

    if ( g_version_check.thread ) g_thread_unref ( g_version_check.thread );
    g_version_check.thread = NULL;

    if ( g_version_check.formdata ) g_free ( g_version_check.formdata );
    g_version_check.formdata = NULL;

#ifdef VERCHECK_USE_LIBSOUP
    if ( g_version_check.session ) g_object_unref ( g_version_check.session );
    g_version_check.session = NULL;
#endif /* VERCHECK_USE_LIBSOUP */

    return;
}
