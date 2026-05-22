/**
 * @file   sdlapp_winmanager_int.h
 * @author Michal Hucik <hucik@ordoz.com>
 * @version 2.0.0
 * @brief  Interní API správce oken — registrace, odregistrace a pomocné funkce.
 *
 * Funkce v tomto hlavičkovém souboru jsou určeny pouze pro interní použití
 * v rámci knihovny sdlapp. Veřejné API je v sdlapp_winmanager.h.
 *
 * @par Changelog:
 * - 2026-03-14: Proběhla kompletní revize a refaktorizace. Vytvořeny unit testy.
 *
 * @par Licence:
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
 */
#ifndef SDLAPP_WINMANAGER_INT_H
#define SDLAPP_WINMANAGER_INT_H

#include <glib.h>
#include "sdlapp_winmanager.h"

/** @brief Zaregistruje okno ve správci oken (interní). */
gboolean sdlapp_winmanager_register_window(SdlAppWindowManager *wm, SdlAppWindow *win);

/** @brief Odregistruje okno ze správce oken (interní). */
void sdlapp_winmanager_unregister_window(SdlAppWindowManager *wm, SdlAppWindow *win);

/** @brief Změní jméno registrovaného okna (interní). */
gboolean sdlapp_winmanager_change_window_name(SdlAppWindowManager *wm, SdlAppWindow *win, const gchar *name);

/** @brief Zjistí, zda existuje alespoň jedno OpenGL okno (interní). */
gboolean sdlapp_winmanager_has_opengl_window(SdlAppWindowManager *wm);

#endif /* SDLAPP_WINMANAGER_INT_H */
