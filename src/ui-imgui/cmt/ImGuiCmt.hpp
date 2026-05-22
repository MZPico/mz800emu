#ifndef IMGUI_CMT_HPP
#define IMGUI_CMT_HPP

#include <vector>
#include <string>

#include "libs/cmtspeed/cmtspeed.h"
#include "cmt/cmtext_block.h"

#include "ui/cmt/UiCmt.hpp"

typedef struct ImGuiCmtSpeedList_t
{
    en_CMTSPEED cmtspeed;
    std::string cmtspeed_txt;
    std::string label_txt;
} ImGuiCmtSpeedList_t;

class ImGuiCmt final
{
public:
    /// Zakázání vytváření instancí.
    ImGuiCmt() = delete;

    /// Zakázání kopírování a přiřazování.
    ImGuiCmt(const ImGuiCmt &) = delete;
    ImGuiCmt &operator=(const ImGuiCmt &) = delete;

    // ziskani pole s rychlostmi pro MZ
    static std::vector<ImGuiCmtSpeedList_t> getMzSpeedList(void);
    static std::vector<ImGuiCmtSpeedList_t> getMzSpeedListDefault(void);

    // ziskani pole s rychlostmi pro ZX
    static std::vector<ImGuiCmtSpeedList_t> getZxSpeedList(void);
};

#endif // IMGUI_CMT_HPP
