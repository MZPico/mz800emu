/* 
 * File:   ff_result.h
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 22. června 2018, 15:11
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


#ifndef FF_RESULT_H
#define FF_RESULT_H

#ifdef __cplusplus
extern "C" {
#endif

    /* Navratove kody FatFS */

    /* File function return code (FRESULT) */

    typedef enum {
        FR_OK = 0, /* (0) Succeeded */
        FR_DISK_ERR, /* (1) A hard error occured in the low level disk I/O layer */
        FR_INT_ERR, /* (2) Assertion failed */
        FR_NOT_READY, /* (3) The physical drive cannot work */
        FR_NO_FILE, /* (4) Could not find the file */
        FR_NO_PATH, /* (5) Could not find the path */
        FR_INVALID_NAME, /* (6) The path name format is invalid */
        FR_DENIED, /* (7) Acces denied due to prohibited access or directory full */
        FR_EXIST, /* (8) Acces denied due to prohibited access */
        FR_INVALID_OBJECT, /* (9) The file/directory object is invalid */
        FR_WRITE_PROTECTED, /* (10) The physical drive is write protected */
        FR_INVALID_DRIVE, /* (11) The logical drive number is invalid */
        FR_NOT_ENABLED, /* (12) The volume has no work area */
        FR_NO_FILESYSTEM, /* (13) There is no valid FAT volume */
        FR_MKFS_ABORTED, /* (14) The f_mkfs() aborted due to any parameter error */
        FR_TIMEOUT, /* (15) Could not get a grant to access the volume within defined period */
        FR_LOCKED, /* (16) The operation is rejected according to the file shareing policy */
        FR_NOT_ENOUGH_CORE, /* (17) LFN working buffer could not be allocated */
        FR_TOO_MANY_OPEN_FILES, /* (18) Number of open files > _FS_SHARE */
        FR_INVALID_PARAMETER /* (19) Given parameter is invalid */
    } FRESULT;




#ifdef __cplusplus
}
#endif

#endif /* FF_RESULT_H */

