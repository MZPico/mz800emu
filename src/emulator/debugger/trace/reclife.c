/**
 * @file   reclife.c
 * @brief  Implementace sdíleného recording lifecycle layeru (reclife).
 *
 * Viz @ref reclife.h pro popis návrhu a kontraktů. Implementace je záměrně
 * minimální - vrstva nedrží žádný stav, jen orchestruje callbacky deskriptoru.
 *
 * @author Michal Hucik <hucik@ordoz.com>
 */

#include "reclife.h"

#include <stddef.h>
#include <assert.h>
#include <glib.h>


void reclife_recompute_active ( const st_RECLIFE_DESC *d, int debugger_active )
{
    assert ( d != NULL );
    assert ( d->mode_ptr != NULL );
    assert ( d->active_ptr != NULL );
    assert ( d->fn_start != NULL );
    assert ( d->fn_stop != NULL );

    int new_active = 0;
    switch ( *d->mode_ptr ) {
        case RECLIFE_MODE_OFF:         new_active = 0; break;
        case RECLIFE_MODE_WITH_WINDOW: new_active = debugger_active; break;
        case RECLIFE_MODE_ALWAYS:      new_active = 1; break;
        default:                       new_active = 0; break;
    }

    if ( new_active && !*d->active_ptr ) {
        if ( d->fn_start ( ) == 0 ) {
            *d->active_ptr = 1;
        }
    } else if ( !new_active && *d->active_ptr ) {
        d->fn_stop ( );
        *d->active_ptr = 0;
    }
}


int reclife_save ( const st_RECLIFE_DESC *d, const char *path )
{
    assert ( d != NULL );

    if ( d->fn_save == NULL ) {
        /* Subsystém nepodporuje save(path). */
        return -1;
    }
    return d->fn_save ( path );
}


int reclife_redirect_path ( char **dir_ptr, char **name_ptr, const char *path )
{
    assert ( dir_ptr != NULL );
    assert ( name_ptr != NULL );

    if ( path == NULL || path[ 0 ] == '\0' ) {
        return -1;
    }

    /* Rozparsovat path na dir + basename. Příponu (.bin/.json/...) NEodřezáváme
     * - tlog writer ji použije jako součást prefixu chunk souborů, takže
     *   "/tmp/seg1" i "/tmp/seg1.cputrack" jsou platné názvy segmentu. */
    char *new_dir  = g_path_get_dirname  ( path );
    char *new_name = g_path_get_basename ( path );

    g_free ( *dir_ptr );
    g_free ( *name_ptr );
    *dir_ptr  = new_dir;
    *name_ptr = new_name;
    return 0;
}


void reclife_finalize ( const st_RECLIFE_DESC *d, int save_on_exit )
{
    assert ( d != NULL );
    assert ( d->active_ptr != NULL );
    assert ( d->fn_stop != NULL );

    if ( save_on_exit && *d->active_ptr ) {
        d->fn_stop ( );
    }
}
