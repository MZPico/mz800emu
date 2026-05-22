#include "main.h"
#include "libs/sdlapp/sdlapp.h"
#include <stdio.h>
#include <glib.h>
#include <vector>
#include <string>

// Lokalizace
#include "i18n.h"

#include "libs/cmtspeed/cmtspeed.h"
#include "hw-generic/cmt/cmtext_block.h"
#include "hw-generic/cmt/cmt_mzf.h"
#include "hw-generic/cmt/cmt_tap.h"

#include "ui/cmt/UiCmt.hpp"
#include "ImGuiCmt.hpp"

struct
{
    std::vector<ImGuiCmtSpeedList_t> mz;
    std::vector<ImGuiCmtSpeedList_t> mz_default;
    std::vector<ImGuiCmtSpeedList_t> zx;
} g_imgui_speeds;

void createList(std::vector<ImGuiCmtSpeedList_t> &list, const std::vector<UiCmtSpeedList_t> &ui_list)
{
    list.clear();
    for (size_t i = 0; i < ui_list.size(); i++)
    {
        ImGuiCmtSpeedList_t item;
        item.cmtspeed = ui_list[i].cmtspeed;
        item.cmtspeed_txt = ui_list[i].cmtspeed_txt;
        item.label_txt = ui_list[i].cmtspeed_txt.c_str();
        item.label_txt += "##speed_row_";
        item.label_txt += std::to_string(i);
        list.push_back(item);
    };
}

std::vector<ImGuiCmtSpeedList_t> ImGuiCmt::getMzSpeedList(void)
{
    if (g_imgui_speeds.mz.empty())
    {
        std::vector<UiCmtSpeedList_t> ui_list = UiCmt::getMzSpeedList();
        createList(g_imgui_speeds.mz, ui_list);
    };
    return g_imgui_speeds.mz;
}

std::vector<ImGuiCmtSpeedList_t> ImGuiCmt::getMzSpeedListDefault(void)
{
    if (g_imgui_speeds.mz_default.empty())
    {
        std::vector<UiCmtSpeedList_t> ui_list = UiCmt::getMzSpeedListDefault();
        createList(g_imgui_speeds.mz_default, ui_list);
    };
    return g_imgui_speeds.mz_default;
}

std::vector<ImGuiCmtSpeedList_t> ImGuiCmt::getZxSpeedList(void)
{
    if (g_imgui_speeds.zx.empty())
    {
        std::vector<UiCmtSpeedList_t> ui_list = UiCmt::getZxSpeedList();
        createList(g_imgui_speeds.zx, ui_list);
    };
    return g_imgui_speeds.zx;
}
