/*
 * File:   baseui_tools.c
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 20. září 2015, 22:05
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "baseui_tools.h"

GError *g_baseui_tools_error;

/*
 *
 * Obecne
 *
 */

const char *baseui_tools_get_error_message(void)
{
    if (NULL == g_baseui_tools_error)
    {
        return "(null error message)";
    }
    return g_baseui_tools_error->message;
}

char *baseui_tools_strerror(void)
{
    return strerror(errno);
}

void baseui_tools_mem_free(void *ptr)
{
    g_free(ptr);
}

void *baseui_tools_mem_alloc_raw(uint32_t size, int initialize0, const char *func, int line)
{
    if (size == 0)
        return NULL;
    void *ptr;
    if (initialize0)
    {
        ptr = g_malloc0(size);
    }
    else
    {
        ptr = g_malloc(size);
    };
    if (ptr == NULL)
    {
        fprintf(stderr, "%s():%d - Could not allocate %u bytes in memory: %s\n", func, line, size, baseui_tools_strerror());
    };
    return ptr;
}

void *baseui_tools_mem_realloc_raw(void *ptr, uint32_t size, const char *func, int line)
{
    if ((ptr == NULL) && (size == 0))
        return NULL;
    if (size == 0)
    {
        baseui_tools_mem_free(ptr);
        return NULL;
    };
    void *newptr = g_realloc(ptr, size);
    if (newptr == NULL)
    {
        fprintf(stderr, "%s():%d - Could not reallocate %u bytes in memory: %s\n", func, line, size, baseui_tools_strerror());
    };
    return newptr;
}

/**
 * basename() z <libgen.h> nefunguje v mingw32
 */
char *baseui_tools_basename(const char *file_name)
{
    return g_path_get_basename(file_name);
}

/*
 *
 * Souborove funkce
 *
 *
 */

char *baseui_tools_file_name_locale_from_utf8(const char *filename_in_utf8)
{
    return g_locale_from_utf8(filename_in_utf8, -1, NULL, NULL, NULL);
}

FILE *baseui_tools_file_open(const char *filename_in_utf8, const char *mode)
{
    char *filename_in_locale;
    FILE *fp;

    filename_in_locale = baseui_tools_file_name_locale_from_utf8(filename_in_utf8);
    fp = fopen(filename_in_locale, mode);
    baseui_tools_mem_free(filename_in_locale);
    return fp;
}

int baseui_tools_file_truncate(const char *filename_in_utf8, off_t length)
{
    char *filename_in_locale = baseui_tools_file_name_locale_from_utf8(filename_in_utf8);
    int ret = truncate(filename_in_locale, length);
    baseui_tools_mem_free(filename_in_locale);
    return ret;
}

void baseui_tools_file_close(FILE *fh)
{
    fclose(fh);
}

unsigned int baseui_tools_file_read(void *buffer, unsigned int size, unsigned int count_bytes, FILE *fh)
{
#ifdef WINDOWS
    /*  stdio bug projevujici se pri "RW" mode :( */
    fseek(fh, ftell(fh), SEEK_SET);
#endif
    return fread(buffer, size, count_bytes, fh);
}

unsigned int baseui_tools_file_write(void *buffer, unsigned int size, unsigned int count_bytes, FILE *fh)
{
#ifdef WINDOWS
    /*  stdio bug projevujici se pri "RW" mode :( */
    fseek(fh, ftell(fh), SEEK_SET);
#endif
    return fwrite(buffer, size, count_bytes, fh);
}

int baseui_tools_file_access(const char *filename_in_utf8, int type)
{
    char *filename_in_locale;
    filename_in_locale = baseui_tools_file_name_locale_from_utf8(filename_in_utf8);
    int retval = access(filename_in_locale, type);
    baseui_tools_mem_free(filename_in_locale);
    return retval;
}

int baseui_tools_file_rename(char *path, char *oldname, char *newname)
{
    char *filepath1 = g_build_filename(path, oldname, (char *)NULL);
    char *filepath2 = g_build_filename(path, newname, (char *)NULL);

    int retval = g_rename(filepath1, filepath2);

    baseui_tools_mem_free((void *)filepath1);
    baseui_tools_mem_free((void *)filepath2);

    return retval;
}

/*
 *
 *
 * Prace s adresari
 *
 *
 */

BASEUI_TOOLS_DIR_HANDLE *baseui_tools_dir_open(char *path)
{
    return g_dir_open(path, 0, &g_baseui_tools_error);
}

void baseui_tools_dir_close(BASEUI_TOOLS_DIR_HANDLE *dh)
{
    g_dir_close(dh);
}

BASEUI_TOOLS_DIR_ITEM *baseui_tools_dir_read(BASEUI_TOOLS_DIR_HANDLE *dh)
{
    return g_dir_read_name(dh);
}

const char *baseui_tools_ditem_get_name(BASEUI_TOOLS_DIR_ITEM *ditem)
{
    return ditem;
}

const char *baseui_tools_ditem_get_filepath(const char *path, BASEUI_TOOLS_DIR_ITEM *ditem)
{
    return g_build_filename(path, ditem, (char *)NULL);
}

unsigned baseui_tools_ditem_is_file(char *path, BASEUI_TOOLS_DIR_ITEM *ditem)
{
    const char *filepath = baseui_tools_ditem_get_filepath(path, ditem);
    unsigned retval = g_file_test(filepath, G_FILE_TEST_IS_REGULAR);
    baseui_tools_mem_free((void *)filepath);
    return retval;
}
