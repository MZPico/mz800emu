#ifndef EMULATOR_MZARCH_PLATFORM_H
#define EMULATOR_MZARCH_PLATFORM_H

#include <stdint.h>

extern const char *g_mzarch_full_name; // full name of the computer, e.g. "MZ-800", "MZ-700", "MZ-1500", "MZ-UNKNOWN"
extern const char *g_mzarch_platform_name; // mzarch name, e.g. "mz800", "mz700", "mz1500"
extern int g_mzarch_platform_numeric; // mzarch numeric, e.g. 800, 700, 1500

extern const uint32_t g_mzarch_platform_pxclk; // pixel clock in Hz

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef __cplusplus
}
#endif

#endif // EMULATOR_MZARCH_PLATFORM_H