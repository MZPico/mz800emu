/* 
 * File:   version_xml_parser.h
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 6. září 2018, 7:33
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


#ifndef VERSION_XML_PARSER_H
#define VERSION_XML_PARSER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <glib.h>

#include "version_check.h"

    extern st_VERSION_REPORT* version_xml_document_parse ( gchar *xmldocument, gsize length, gboolean quiet );

#ifdef __cplusplus
}
#endif

#endif /* VERSION_XML_PARSER_H */

