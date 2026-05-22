/* 
 * File:   version_check.h
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 28. srpna 2018, 7:47
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


#ifndef VERSION_CHECK_H
#define VERSION_CHECK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <glib.h>

#define VERSION_CHECK_URL "https://www.ordoz.com/mz800emu/version_check/"

#define VERSION_CHECK_MAJOR_BRANCH "stable"


    typedef enum en_VERSION_CHECK_PERIOD {
        VERSION_CHECK_PERIOD_NEVER = 0,
        VERSION_CHECK_PERIOD_DAILY = 1,
        VERSION_CHECK_PERIOD_WEEKLY = 7,
        VERSION_CHECK_PERIOD_MONTHLY = 30,
        VERSION_CHECK_PERIOD_SOMETIMES = 99,
        VERSION_CHECK_PERIOD_UNKNOWN = 999,
    } en_VERSION_CHECK_PERIOD;

#define VERSION_CHECK_DEFAULT_PERIOD VERSION_CHECK_PERIOD_MONTHLY


    typedef enum en_VERSION_CHECK_PROXY {
        VERSION_CHECK_PROXY_NONE = 0,
        VERSION_CHECK_PROXY_SYSTEM,
        VERSION_CHECK_PROXY_MANUAL,
    } en_VERSION_CHECK_PROXY;


    extern gboolean g_version_check_thread_done;

#define VERSION_CHECK_TEST_THREAD_DONE  ( g_version_check_thread_done == TRUE )


    typedef struct st_VERSION_BRANCH {
        guint32 elm_version;
        GString *tag;
        guint32 version;
        guint32 revision;
        GString *date;
        GString *msg;
    } st_VERSION_BRANCH;


    typedef struct st_VERSION_REPORT {
        guint32 elm_version;
        guint32 count_branch;
        st_VERSION_BRANCH **branch;
    } st_VERSION_REPORT;


    extern void version_check_init ( void );
    extern void version_check_exit ( void );

    extern void version_check_run_test ( gboolean manual );
    extern void version_check_parse_thread_response ( void );

    extern uint32_t version_check_get_unix_days ( void );
    extern en_VERSION_CHECK_PERIOD version_check_get_period ( void );
    extern void version_check_set_period ( en_VERSION_CHECK_PERIOD period );
    extern uint32_t version_check_get_last_check ( void );
    extern void version_check_update_last_check ( void );
    extern void version_check_set_ignored ( char *tag, uint32_t version, uint32_t revision );
    extern en_VERSION_CHECK_PROXY version_check_get_proxy_settings ( void );
    extern void version_check_set_proxy_settings ( en_VERSION_CHECK_PROXY use_proxy );
    extern const char* version_check_get_proxy_server ( void );
    extern void version_check_set_proxy_server ( const char *proxy_server );

    extern st_VERSION_REPORT* version_check_report_new ( guint32 elm_version );
    extern void version_check_report_free ( st_VERSION_REPORT *report );
    extern st_VERSION_BRANCH* version_check_branch_new ( guint32 elm_version, gchar *tag, guint32 version, guint32 revision, gchar *date );
    extern void version_check_branch_free ( st_VERSION_BRANCH *branch );
    extern void version_check_branch_set_msg ( st_VERSION_BRANCH *branch, const gchar *msg );
    extern st_VERSION_BRANCH* version_check_copy_branch ( st_VERSION_BRANCH *branch );
    extern void version_check_report_add_branch ( st_VERSION_REPORT *report, st_VERSION_BRANCH *branch );
    extern GString* version_check_create_version_string ( guint32 version );

#ifdef __cplusplus
}
#endif

#endif /* VERSION_CHECK_H */

