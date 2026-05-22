#ifndef IMGUI_FILECHOOSER_H
#define IMGUI_FILECHOOSER_H

#include <stdint.h>
#include <stdbool.h>
#include <glib.h>
#include "baseui/baseui_filechooser.h"

#ifdef __cplusplus
extern "C"
{
#endif

    void imgui_filechooser_new(baseui_fchooser_t *fch);

#ifdef __cplusplus
}
#endif

#endif /* IMGUI_FILECHOOSER_H */
