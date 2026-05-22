/**
 * @file snapshot_screenshot.h
 * @brief Generování PNG screenshotu z framebufferu pro snapshot systém
 */

#ifndef SNAPSHOT_SCREENSHOT_H
#define SNAPSHOT_SCREENSHOT_H

#include "snapshot.h"
#include "snapshot_io.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Uložení screenshotu aktuálního framebufferu do ZIP archivu jako screenshot.png
 *
 * Vezme aktuální framebuffer pixely, aplikuje paletu barev, konvertuje na RGBA
 * a uloží jako PNG do archivu.
 *
 * @param io Handle na otevřený ZIP archiv (režim zápis)
 * @return SNAPSHOT_OK při úspěchu
 */
en_SNAPSHOT_RESULT snapshot_screenshot_save(snapshot_io_t *io);

#ifdef __cplusplus
}
#endif

#endif /* SNAPSHOT_SCREENSHOT_H */
