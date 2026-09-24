#include "Widgets.hpp"

#include "GUI/Theme.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <cstring>

namespace mc::widgets {

float EaseOutExpo(float t)
{
    return (t >= 1.0f) ? 1.0f : 1.0f - powf(2.0f, -10.0f * t);
}

static float EaseInExpo(float t)
{
    return (t <= 0.0f) ? 0.0f : powf(2.0f, 10.0f * (t - 1.0f));
}

static const char* labelEnd(const char* s)
{
    const char* h = strstr(s, "##");
    return h ? h : s + (int)strlen(s);
}

static ImU32 accentU32(float alpha)
{
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z, alpha));
}

bool CCombo(const char* label, int* current_item, const std::vector<std::string>& items)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    const float width = ImGui::CalcItemWidth();
    const float height = ImGui::GetFrameHeight();
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));

    ImGui::ItemSize(bb, style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered = false;
    bool held = false;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    const ImGuiID popup_id = ImHashStr("##CComboPopup", 0, id);
    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID prevKey = ImHashStr("##CComboPrev", 0, id);
    const ImGuiID closeKey = ImHashStr("##CComboClosing", 0, id);

    bool openPrev = storage->GetInt(prevKey, 0) != 0;
    bool pendingClose = storage->GetInt(closeKey, 0) != 0;

    if (pressed)
    {
        if (pendingClose)
        {
            pendingClose = false;
        }
        else if (openPrev || ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None))
        {
            if (ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None))
                ImGui::ClosePopupsOverWindow(window->RootWindow, false);
            pendingClose = false;
        }
        else
        {
            ImGui::OpenPopupEx(popup_id, ImGuiPopupFlags_None);
        }
    }

    bool openNow = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
    if (!openNow)
        pendingClose = false;

    float anim_progress = storage->GetFloat(id, 0.0f);
    const float anim_speed = 5.0f * g.IO.DeltaTime;
    if (openNow && !pendingClose)
        anim_progress = ImMin(anim_progress + anim_speed, 1.0f);
    else
        anim_progress = ImMax(anim_progress - anim_speed, 0.0f);
    storage->SetFloat(id, anim_progress);
    storage->SetInt(closeKey, pendingClose ? 1 : 0);
    storage->SetInt(prevKey, openNow ? 1 : 0);

    ImDrawList* draw_list = window->DrawList;

    ImU32 bg_color = ImGui::GetColorU32(hovered ? ImVec4(0.155f, 0.175f, 0.210f, 1.00f)
                                                : ImVec4(0.110f, 0.125f, 0.150f, 1.00f));
    ImU32 border_color = openNow ? accentU32(1.0f)
                                 : ImGui::GetColorU32(ImVec4(0.19f, 0.21f, 0.25f, 1.00f));
    ImU32 text_color = ImGui::GetColorU32(ImVec4(0.92f, 0.93f, 0.95f, 1.00f));
    ImU32 arrow_color = ImGui::GetColorU32(ImVec4(0.70f, 0.70f, 0.74f, 1.00f));

    const float rounding = 6.0f;

    draw_list->AddRectFilled(bb.Min, bb.Max, bg_color, rounding);
    draw_list->AddRect(bb.Min, bb.Max, border_color, rounding, 0, openNow ? 1.5f : 1.0f);

    const char* item_text =
        (*current_item >= 0 && *current_item < (int)items.size()) ? items[*current_item].c_str() : "";
    ImVec2 text_size = ImGui::CalcTextSize(item_text);
    float text_y_pos = bb.Min.y + std::floor((height - text_size.y) * 0.5f);
    ImVec2 text_pos = ImVec2(bb.Min.x + style.FramePadding.x, text_y_pos);
    draw_list->AddText(g.Font, g.FontSize, text_pos, text_color, item_text, nullptr, 0.0f, nullptr);

    ImVec2 arrow_center = ImVec2(bb.Max.x - 14.0f, bb.Min.y + std::floor(height * 0.5f));
    const ImVec2 arrow_pts[3] = {
        ImVec2(arrow_center.x - 4.0f, arrow_center.y - 2.0f),
        ImVec2(arrow_center.x, arrow_center.y + 2.0f),
        ImVec2(arrow_center.x + 4.0f, arrow_center.y - 2.0f),
    };
    draw_list->AddPolyline(arrow_pts, 3, arrow_color, 0, 1.5f);

    if (label && *label)
    {
        const char* end = labelEnd(label);
        if (end > label)
        {
            ImVec2 label_size = ImGui::CalcTextSize(label, end);
            float label_y_pos = bb.Min.y + std::floor((height - label_size.y) * 0.5f);
            ImGui::SameLine(0, style.ItemInnerSpacing.x);
            ImGui::SetCursorPosY(label_y_pos - window->Pos.y);
            ImGui::TextUnformatted(label, end);
        }
    }

    bool value_changed = false;

    if (openNow)
    {
        float eased_progress = pendingClose ? EaseInExpo(anim_progress)
                                            : EaseOutExpo(anim_progress);

        const float item_height = 28.0f;
        const float max_popup_height =
            ImMin((float)items.size() * item_height + style.WindowPadding.y * 2.0f, 200.0f);
        const float current_popup_height = ImMax(max_popup_height * eased_progress, 4.0f);
        const float current_alpha = ImMin(eased_progress * 1.2f, 1.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg,
                              ImVec4(0.070f, 0.080f, 0.095f, current_alpha * 0.97f));
        ImGui::PushStyleColor(ImGuiCol_Border,
                              ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z,
                                     current_alpha * 0.5f));

        ImGui::SetNextWindowPos(ImVec2(bb.Min.x, bb.Max.y + 4.0f));
        ImGui::SetNextWindowSize(ImVec2(width, current_popup_height));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoScrollbar;

        if (ImGui::BeginPopupEx(popup_id, flags))
        {
            for (int i = 0; i < (int)items.size(); i++)
            {
                ImGui::PushID(i);
                bool is_selected = (*current_item == i);

                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                ImGui::PushStyleColor(ImGuiCol_Header,
                                      ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z,
                                             current_alpha * 0.35f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                      ImVec4(0.155f, 0.175f, 0.210f, current_alpha));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                                      ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z,
                                             current_alpha * 0.5f));

                ImVec2 cursor_before = ImGui::GetCursorPos();
                if (ImGui::Selectable("##item", is_selected, 0, ImVec2(0, item_height)))
                {
                    *current_item = i;
                    value_changed = true;
                    pendingClose = true;
                }
                ImVec2 after_selectable = ImGui::GetCursorPos();

                ImVec2 item_text_size = ImGui::CalcTextSize(items[i].c_str());
                float item_text_y =
                    cursor_before.y + std::floor((item_height - item_text_size.y) * 0.5f);
                ImGui::SetCursorPos(ImVec2(cursor_before.x + style.FramePadding.x, item_text_y));
                ImGui::TextUnformatted(items[i].c_str());

                ImGui::SetCursorPos(after_selectable);

                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar();

                if (is_selected)
                    ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }

            if (pendingClose && anim_progress <= 0.001f)
            {
                ImGui::CloseCurrentPopup();
                pendingClose = false;
                anim_progress = 0.0f;
                storage->SetFloat(id, 0.0f);
            }

            ImGui::EndPopup();
        }
        else
        {
            pendingClose = false;
        }

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    storage->SetInt(closeKey, pendingClose ? 1 : 0);
    storage->SetInt(prevKey, ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None) ? 1 : 0);

    return value_changed;
}

bool CCombo(const char* label, int* current_item, const char* const items[], int count)
{
    std::vector<std::string> tmp;
    tmp.reserve((size_t)count);
    for (int i = 0; i < count; ++i)
        tmp.emplace_back(items[i]);
    return CCombo(label, current_item, tmp);
}

bool CSlider(const char* label, float* v, float v_min, float v_max, const char* format)
{
    return ImGui::SliderFloat(label, v, v_min, v_max, format);
}

bool CSlider(const char* label, int* v, int v_min, int v_max, const char* format)
{
    return ImGui::SliderInt(label, v, v_min, v_max, format);
}

}
