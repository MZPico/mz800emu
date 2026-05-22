#pragma once

// ############################# DISCLAIMER #############################
// ############ THIS WIDGTES LIB IS BASED ON IMGUIPACK ##################
// ############# https://github.com/aiekick/ImGuiPack ###################
// ######################################################################

#include "ui-imgui/filechooser/res/CustomFont.h"

#define BUTTON_LABEL_RESET ICON_IGFD_RESET
#define BUTTON_LABEL_PLUS ICON_IGFD_ADD
#define BUTTON_LABEL_MINUS ICON_IGFD_REMOVE

#define MAX_PATH 2048

#if defined(LINUX)
#include <unistd.h>
#endif  // LINUX
