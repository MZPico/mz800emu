#include "main.h"
#include "libs/sdlapp/sdlapp.h"
#include "ui-imgui/bootstrap/myimgui.h"
#include "libs/imgui/imgui.h"
#include <stdio.h>
#include <glib.h>
#include <string>

// Lokalizace
#include "i18n.h"

#include "ui-imgui/filechooser/res/CustomFont.h"
#include "CmtGlyphRenderer.h"

CmtGlyphRenderer::CmtGlyphRenderer(float glyphHeight, float vlineWidth) : GLYPH_HEIGHT(glyphHeight), GLYPH_VLINE_WIDTH(vlineWidth)
{
    GLYPH_TRIANGLE_WIDTH = GLYPH_HEIGHT * 0.8f;
    GLYPH_TRIANGLE_HEIGHT = GLYPH_HEIGHT;
    GLYPH_STOP_WIDTH = GLYPH_HEIGHT * 0.8f;
    GLYPH_STOP_HEIGHT = GLYPH_HEIGHT * 0.8f;
    GLYPH_RECORD_RADIUS = GLYPH_HEIGHT * 0.4f;
}

void CmtGlyphRenderer::drawPrevious(void)
{
    ImVec2 pos = ImGui::GetItemRectMin();
    ImVec2 size = ImGui::GetItemRectSize();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);

    float center_x = pos.x + size.x / 2;
    float center_y = pos.y + size.y / 2;

    float glyph_width = GLYPH_TRIANGLE_WIDTH + GLYPH_VLINE_WIDTH;
    float xpos = center_x - (glyph_width / 2);

    // Vykreslení svislé čáry (vlevo)
    draw_list->AddLine(
        ImVec2(xpos, center_y - GLYPH_TRIANGLE_HEIGHT / 2),
        ImVec2(xpos, center_y + GLYPH_TRIANGLE_HEIGHT / 2),
        color,
        GLYPH_VLINE_WIDTH);

    xpos += GLYPH_VLINE_WIDTH;

    // Trojúhelník směřující doleva
    draw_list->AddTriangleFilled(
        ImVec2(xpos, center_y),
        ImVec2(xpos + GLYPH_TRIANGLE_WIDTH, center_y - GLYPH_TRIANGLE_HEIGHT / 2),
        ImVec2(xpos + GLYPH_TRIANGLE_WIDTH, center_y + GLYPH_TRIANGLE_HEIGHT / 2),
        color);
}

void CmtGlyphRenderer::drawBackward(void)
{
    ImVec2 pos = ImGui::GetItemRectMin();
    ImVec2 size = ImGui::GetItemRectSize();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);

    float center_x = pos.x + size.x / 2;
    float center_y = pos.y + size.y / 2;

    float glyph_width = GLYPH_TRIANGLE_WIDTH * 2;
    float xpos = center_x - (glyph_width / 2);

    // Levý trojúhelník směřující doleva
    draw_list->AddTriangleFilled(
        ImVec2(xpos, center_y),
        ImVec2(xpos + GLYPH_TRIANGLE_WIDTH, center_y - GLYPH_TRIANGLE_HEIGHT / 2),
        ImVec2(xpos + GLYPH_TRIANGLE_WIDTH, center_y + GLYPH_TRIANGLE_HEIGHT / 2),
        color);

    xpos += GLYPH_TRIANGLE_WIDTH;

    // Pravý trojúhelník směřující doleva
    draw_list->AddTriangleFilled(
        ImVec2(xpos, center_y),
        ImVec2(xpos + GLYPH_TRIANGLE_WIDTH, center_y - GLYPH_TRIANGLE_HEIGHT / 2),
        ImVec2(xpos + GLYPH_TRIANGLE_WIDTH, center_y + GLYPH_TRIANGLE_HEIGHT / 2),
        color);
}

void CmtGlyphRenderer::drawForward(void)
{
    ImVec2 pos = ImGui::GetItemRectMin();
    ImVec2 size = ImGui::GetItemRectSize();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);

    float center_x = pos.x + size.x / 2;
    float center_y = pos.y + size.y / 2;

    float glyph_width = GLYPH_TRIANGLE_WIDTH * 1;
    float glyph_xpos = center_x - (glyph_width / 2);

    // Levý trojúhelník směřující doprava
    draw_list->AddTriangleFilled(
        ImVec2(glyph_xpos + GLYPH_TRIANGLE_WIDTH, center_y),
        ImVec2(glyph_xpos, center_y - GLYPH_TRIANGLE_HEIGHT / 2),
        ImVec2(glyph_xpos, center_y + GLYPH_TRIANGLE_HEIGHT / 2),
        color);

    glyph_xpos += GLYPH_TRIANGLE_WIDTH + 2;

    // Pravý trojúhelník směřující doprava
    draw_list->AddTriangleFilled(
        ImVec2(glyph_xpos + GLYPH_TRIANGLE_WIDTH, center_y),
        ImVec2(glyph_xpos, center_y - GLYPH_TRIANGLE_HEIGHT / 2),
        ImVec2(glyph_xpos, center_y + GLYPH_TRIANGLE_HEIGHT / 2),
        color);
}

void CmtGlyphRenderer::drawNext(void)
{
    ImVec2 pos = ImGui::GetItemRectMin();
    ImVec2 size = ImGui::GetItemRectSize();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);

    float center_x = pos.x + size.x / 2;
    float center_y = pos.y + size.y / 2;

    float glyph_width = GLYPH_TRIANGLE_WIDTH + GLYPH_VLINE_WIDTH;
    float xpos = center_x - (glyph_width / 2);

    // Trojúhelník směřující doprava
    draw_list->AddTriangleFilled(
        ImVec2(xpos, center_y - GLYPH_TRIANGLE_HEIGHT / 2),
        ImVec2(xpos, center_y + GLYPH_TRIANGLE_HEIGHT / 2),
        ImVec2(xpos + GLYPH_TRIANGLE_WIDTH, center_y),
        color);

    xpos += GLYPH_TRIANGLE_WIDTH + GLYPH_VLINE_WIDTH;

    // Svislá čára (vpravo)
    draw_list->AddLine(
        ImVec2(xpos, center_y - GLYPH_TRIANGLE_HEIGHT / 2),
        ImVec2(xpos, center_y + GLYPH_TRIANGLE_HEIGHT / 2),
        color,
        GLYPH_VLINE_WIDTH);
}

void CmtGlyphRenderer::drawStop(void)
{
    ImVec2 pos = ImGui::GetItemRectMin();
    ImVec2 size = ImGui::GetItemRectSize();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);

    float center_x = pos.x + size.x / 2;
    float center_y = pos.y + size.y / 2;

    ImVec2 top_left(center_x - GLYPH_STOP_WIDTH / 2, center_y - GLYPH_STOP_HEIGHT / 2);
    ImVec2 bottom_right(center_x + GLYPH_STOP_WIDTH / 2, center_y + GLYPH_STOP_HEIGHT / 2);

    draw_list->AddRectFilled(top_left, bottom_right, color);
}

void CmtGlyphRenderer::drawRecord(bool active)
{
    ImVec2 pos = ImGui::GetItemRectMin();
    ImVec2 size = ImGui::GetItemRectSize();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 color = (!active) ? ImGui::GetColorU32(ImGuiCol_Text) : IM_COL32(255, 0, 0, 255);

    float center_x = pos.x + size.x / 2;
    float center_y = pos.y + size.y / 2;

    draw_list->AddCircleFilled(ImVec2(center_x, center_y), GLYPH_RECORD_RADIUS, color);
}

void CmtGlyphRenderer::drawPlay(bool active)
{
    ImVec2 pos = ImGui::GetItemRectMin();
    ImVec2 size = ImGui::GetItemRectSize();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 color = (!active) ? ImGui::GetColorU32(ImGuiCol_Text) : IM_COL32(0, 255, 0, 255);

    float center_x = pos.x + size.x / 2;
    float center_y = pos.y + size.y / 2;

    float glyph_width = GLYPH_TRIANGLE_WIDTH;
    float xpos = center_x; // - glyph_width / 2;

    draw_list->AddTriangleFilled(
        ImVec2(xpos, center_y - GLYPH_TRIANGLE_HEIGHT / 2),
        ImVec2(xpos, center_y + GLYPH_TRIANGLE_HEIGHT / 2),
        ImVec2(xpos + glyph_width, center_y),
        color);
}

void CmtGlyphRenderer::drawPause(bool active)
{
    ImVec2 pos = ImGui::GetItemRectMin();
    ImVec2 size = ImGui::GetItemRectSize();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 color = (!active) ? ImGui::GetColorU32(ImGuiCol_Text) : IM_COL32(0, 255, 0, 255);

    float bar_width = GLYPH_VLINE_WIDTH;
    float spacing = bar_width * 1.0f;

    float center_x = pos.x + size.x / 2;
    float center_y = pos.y + size.y / 2;
    float bar_height = GLYPH_HEIGHT;

    float left_x = center_x - spacing / 2 - bar_width;
    float right_x = center_x + spacing / 2;

    ImVec2 top(bar_height / 2, -bar_height / 2);

    draw_list->AddRectFilled(
        ImVec2(left_x, center_y - bar_height / 2),
        ImVec2(left_x + bar_width, center_y + bar_height / 2),
        color);

    draw_list->AddRectFilled(
        ImVec2(right_x, center_y - bar_height / 2),
        ImVec2(right_x + bar_width, center_y + bar_height / 2),
        color);
}

void CmtGlyphRenderer::drawEject(void)
{
    ImVec2 pos = ImGui::GetItemRectMin();
    ImVec2 size = ImGui::GetItemRectSize();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);

    float center_x = pos.x + size.x / 2;
    float center_y = pos.y + size.y / 2;

    float triangle_height = GLYPH_HEIGHT * 0.7f;
    float triangle_width = GLYPH_TRIANGLE_WIDTH;
    float line_height = GLYPH_VLINE_WIDTH;
    float spacing = 8.0f;

    float glyph_top = center_y - (triangle_height + spacing + line_height) / 2 + 1.5f;

    // Trojúhelník směřující nahoru
    draw_list->AddTriangleFilled(
        ImVec2(center_x, glyph_top), // vrchol
        ImVec2(center_x - triangle_width / 2, glyph_top + triangle_height),
        ImVec2(center_x + triangle_width / 2, glyph_top + triangle_height),
        color);

    // Čára pod trojúhelníkem
    draw_list->AddLine(
        ImVec2(center_x - triangle_width / 2, glyph_top + triangle_height + spacing),
        ImVec2(center_x + triangle_width / 2, glyph_top + triangle_height + spacing),
        color,
        line_height);
}
