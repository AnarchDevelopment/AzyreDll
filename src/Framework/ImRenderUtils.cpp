#include "ImRenderUtils.hpp"

#include <imgui_internal.h>

namespace mc::renderutils {

namespace {

ImDrawList* foreground(ImDrawList* d)
{
    return d ? d : ImGui::GetForegroundDrawList();
}

} // namespace

float getTextWidth(const char* text, float size)
{
    return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x;
}

float getTextHeight(float size)
{
    return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, "").y;
}

void fillRectangle(const ImVec4& r, ImU32 col, float alpha, float rounding,
                   ImDrawFlags flags, ImDrawList* d)
{
    if (alpha <= 0.001f)
        return;
    float baseA = ((col >> 24) & 0xFF) / 255.0f;
    ImU32 scaled = (col & 0x00FFFFFFu) | ((unsigned)((int)(baseA * alpha * 255.0f) << 24));
    foreground(d)->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.z, r.w), scaled,
                                 rounding, flags);
}

void fillRoundedGradientRectangle(const ImVec4& r, ImU32 c1, ImU32 c2, float radius,
                                  float a1, float a2, ImDrawFlags flags, ImDrawList* d)
{
    if (a1 <= 0.001f && a2 <= 0.001f)
        return;
    ImDrawList* list = foreground(d);
    ImVec2 topLeft(r.x, r.y);
    ImVec2 bottomRight(r.z, r.w);

    ImU32 g1 = (c1 & 0x00FFFFFFu) | ((unsigned)((int)(a1 * 255.0f) << 24));
    ImU32 g2 = (c2 & 0x00FFFFFFu) | ((unsigned)((int)(a2 * 255.0f) << 24));

    // El truco del original: dos pasadas de rect con el shading por vertice
    // (conserva el alpha de cada pasada).
    int start = list->VtxBuffer.Size;
    list->AddRectFilled(topLeft, bottomRight, g1, radius, flags);
    int end = list->VtxBuffer.Size;
    list->AddRectFilled(topLeft, bottomRight, g1, radius, flags);
    int end2 = list->VtxBuffer.Size;

    ImGui::ShadeVertsLinearColorGradientKeepAlpha(list, start, end, topLeft, bottomRight, g1, g2);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(list, end, end2, topLeft, bottomRight, g1, g2);
}

void drawText(const ImVec2& pos, const char* text, ImU32 col, float size,
              float alpha, bool shadow, ImDrawList* d, ImFont* font)
{
    ImFont* f = font ? font : ImGui::GetFont();
    if (shadow)
    {
        ImU32 sc = IM_COL32(0, 0, 0, (int)(alpha * 255.0f));
        foreground(d)->AddText(f, size, ImVec2(pos.x + 1.0f, pos.y + 1.0f), sc, text);
    }
    ImU32 tc = (col & 0x00FFFFFFu) | ((unsigned)((int)(alpha * 255.0f) << 24));
    foreground(d)->AddText(f, size, pos, tc, text);
}

bool isMouseOver(const ImVec4& r)
{
    ImVec2 m = ImGui::GetIO().MousePos;
    return m.x >= r.x && m.x <= r.z && m.y >= r.y && m.y <= r.w;
}

} // namespace
