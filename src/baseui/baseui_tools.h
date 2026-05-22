/* 
 * File:   baseui_tools.h
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 20. září 2015, 22:06
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

#ifndef BASEUI_TOOLS_H
#define BASEUI_TOOLS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <glib.h>
#include <sys/types.h>

#include <stdlib.h>

    /*
     * 
     * Obecne
     * 
     */
    extern const char* baseui_tools_get_error_message ( void );
    extern char* baseui_tools_strerror ( void );
    extern void baseui_tools_mem_free ( void *ptr );



/*
 *
 * Alokovani pameti
 * 
 */
    void* baseui_tools_mem_alloc_raw ( uint32_t size, int initialize0, const char *func, int line );
#define baseui_tools_mem_alloc(size) baseui_tools_mem_alloc_raw ( size, 0, __func__, __LINE__ )
#define baseui_tools_mem_alloc0(size) baseui_tools_mem_alloc_raw ( size, 1, __func__, __LINE__ )
    extern void* baseui_tools_mem_realloc_raw ( void *ptr, uint32_t size, const char *func, int line );
#define baseui_tools_mem_realloc(ptr,size) baseui_tools_mem_realloc_raw ( ptr, size, __func__, __LINE__ )



    extern char* baseui_tools_basename ( const char *file_name );

    /*
     * 
     * Prace se soubory
     * 
     */
    extern char* baseui_tools_file_name_locale_from_utf8 ( const char *filename_in_utf8 );
    extern FILE* baseui_tools_file_open ( const char *filename_in_utf8, const char *mode );
    extern void baseui_tools_file_close ( FILE *fh );
    extern unsigned int baseui_tools_file_read ( void *buffer, unsigned int size, unsigned int count_bytes, FILE *fh );
    extern unsigned int baseui_tools_file_write ( void *buffer, unsigned int size, unsigned int count_bytes, FILE *fh );
    extern int baseui_tools_file_access ( const char *filename_in_utf8, int type );
    extern int baseui_tools_file_rename ( char *path, char *oldname, char *newname );
    extern int baseui_tools_file_truncate ( const char *filename_in_utf8, off_t length );

    /*
     * 
     * Adresarove funkce
     * 
     */
    typedef GDir BASEUI_TOOLS_DIR_HANDLE;
    typedef const char BASEUI_TOOLS_DIR_ITEM;

    extern BASEUI_TOOLS_DIR_HANDLE* baseui_tools_dir_open ( char *path );
    extern void baseui_tools_dir_close ( BASEUI_TOOLS_DIR_HANDLE *dh );

    extern BASEUI_TOOLS_DIR_ITEM* baseui_tools_dir_read ( BASEUI_TOOLS_DIR_HANDLE *dh );

    extern unsigned baseui_tools_ditem_is_file ( char *path, BASEUI_TOOLS_DIR_ITEM *ditem );
    extern const char* baseui_tools_ditem_get_filepath ( const char *path, BASEUI_TOOLS_DIR_ITEM *ditem );

    extern const char* baseui_tools_ditem_get_name ( BASEUI_TOOLS_DIR_ITEM *ditem );


#define baseui_tools_build_filepath( path, filename ) baseui_tools_ditem_get_filepath ( path, filename )
#define baseui_tools_free_filepath( filepath ) baseui_tools_mem_free ( (void *) filepath )

#ifdef __cplusplus
}
#endif

#endif /* BASEUI_TOOLS_H */

