#ifndef IMGUI_VERSION_CHECK_H
#define IMGUI_VERSION_CHECK_H

#include <stdint.h>
#include <stdbool.h>
#include "version_check/version_check.h"

#ifdef __cplusplus
extern "C"
{
#endif
    void imgui_version_check_setup_window(bool *p_open);
    void imgui_version_check_result_window(bool *p_open);
    void imgui_version_check_report_done(st_VERSION_BRANCH *branch);
#ifdef __cplusplus
}
#endif

#endif /* IMGUI_VERSION_CHECK_H */
