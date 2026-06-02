/*
 * png_encode.h — wrapper nad stb_image_write.h pro enkódování RGBA do PNG
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

/**
 * @file png_encode.h
 * @brief Enkódování RGBA8888 framebufferu do PNG streamu v paměti.
 *
 * Tenký wrapper nad vendorovaným `stb_image_write.h` (public domain,
 * Sean Barrett). Implementace stb knihovny žije v jediném translation
 * unitu `png_encode.c` (= `STB_IMAGE_WRITE_IMPLEMENTATION`).
 *
 * Modul je arch-independent (žádný MZARCH dispatch) a kompiluje se do
 * všech build targetů (mz800/mz700-pal/mz700-ntsc/mz1500) přes glob
 * `src/emulator/debugger` v AddMzEmu.cmake.
 */

#ifndef PNG_ENCODE_H
#define PNG_ENCODE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Zenkóduje RGBA8888 buffer do PNG file streamu v paměti.
 *
 * Pixely jsou interpretovány jako těsně uložené (= bez paddingu) řádky
 * o šířce `width` pixelů, každý pixel 4 bajty v pořadí R, G, B, A.
 *
 * @param rgba    Vstupní RGBA8888 buffer; musí mít `width*height*4` bajtů.
 *                Nesmí být NULL.
 * @param width   Šířka obrázku v pixelech; musí být > 0.
 * @param height  Výška obrázku v pixelech; musí být > 0.
 * @param out_len OUT: délka vráceného PNG streamu v bajtech. Nesmí být
 *                NULL. Při chybě se nastaví na 0.
 *
 * @return Pointer na nově alokovaný buffer s PNG streamem (alokováno
 *         přes glib, viz pravidla vlastnictví), nebo NULL při chybě
 *         (neplatné parametry nebo selhání enkódování/alokace).
 *
 * @note Pravidla vlastnictví: vrácený buffer vlastní volající a musí ho
 *       uvolnit přes `g_free()`. Wrapper interně používá glib alokátor,
 *       takže `g_free()` je správný protějšek (žádný malloc/free
 *       mismatch s libc).
 *
 * @note Side effects: žádné globální. Funkce je čistá vůči vstupu
 *       (buffer `rgba` se nemění).
 *
 * @note Thread safety: reentrantní, nepoužívá globální stav. V
 *       emulátoru se volá z emu vlákna v CMDRQ dispatch kontextu.
 */
uint8_t *png_encode_rgba ( const uint8_t *rgba, uint32_t width,
                           uint32_t height, size_t *out_len );

#ifdef __cplusplus
}
#endif

#endif /* PNG_ENCODE_H */
