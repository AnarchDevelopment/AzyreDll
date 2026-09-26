#include "Widgets.hpp"

#include "GUI/Theme.hpp"
#include "Render/DX11.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <cstring>
#include <map>

namespace mc::widgets {

float EaseOutExpo(float t)
{
    return (t >= 1.0f) ? 1.0f : 1.0f - powf(2.0f, -10.0f * t);
}

// Sombra suave (glow negro): capas de rects redondeados con caida cubica.
void SoftShadow(ImDrawList* d, const ImVec2& min, const ImVec2& max,
                float rounding, float strength, float spread)
{
    const int layers = 9;
    for (int i = 0; i < layers; ++i)
    {
        float t = (float)i / (float)(layers - 1);
        float pad = spread * t;
        float f = 1.0f - t;
        f = f * f * f;
        int a = (int)(strength * f);
        if (a <= 0)
            continue;
        d->AddRectFilled(ImVec2(min.x - pad, min.y - pad),
                         ImVec2(max.x + pad, max.y + pad),
                         IM_COL32(0, 0, 0, a), rounding + pad);
    }
}

// Panel acrilico: blur local (sub-rect del backdrop global) + tarjeta
// translucida + borde sutil, con sombra por debajo.
void AcrylicPanel(ImDrawList* d, const ImVec2& min, const ImVec2& max,
                  float rounding, float alpha, float sw, float sh,
                  float shadowStrength, float shadowSpread)
{
    if (alpha <= 0.01f)
        return;

    SoftShadow(d, min, max, rounding, shadowStrength * alpha, shadowSpread);

    // Blur solo cuando hay captura fresca (menu abierto); en juego la tarjeta
    // solida translucida evita tocar la swapchain del juego.
    bool haveBlur = dx11::backdropWanted() && dx11::backdropReady();
    if (haveBlur)
    {
        ImVec2 uv0(min.x / sw, min.y / sh);
        ImVec2 uv1(max.x / sw, max.y / sh);
        d->AddImageRounded(ImTextureRef((void*)dx11::backdropSrv()),
                           min, max, uv0, uv1,
                           IM_COL32(255, 255, 255, (int)(255.0f * alpha)), rounding);
    }

    // Scrim minimo (acrilico autentico): el blur manda, la tarjeta apenas tinta.
    if (haveBlur)
        d->AddRectFilled(min, max, IM_COL32(16, 20, 28, (int)(78.0f * alpha)), rounding);
    else
        d->AddRectFilled(min, max, IM_COL32(31, 31, 33, (int)(200.0f * alpha)), rounding);
    d->AddRect(min, max, IM_COL32(255, 255, 255, (int)(22.0f * alpha)), rounding, 0, 1.0f);
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

// ============================ CCombo ============================

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

    // WinUI: relleno subtle (blanco 6%), hover blanco 10%.
    ImU32 bg_color = ImGui::GetColorU32(hovered ? ImVec4(1.00f, 1.00f, 1.00f, 0.105f)
                                                : ImVec4(1.00f, 1.00f, 1.00f, 0.061f));
    ImU32 border_color = openNow ? accentU32(0.90f)
                                 : ImGui::GetColorU32(ImVec4(1.00f, 1.00f, 1.00f, 0.12f));
    ImU32 text_color = ImGui::GetColorU32(ImVec4(0.953f, 0.953f, 0.961f, 1.00f));
    ImU32 arrow_color = ImGui::GetColorU32(ImVec4(1.00f, 1.00f, 1.00f, 0.60f));

    const float rounding = 4.0f;

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
            draw_list->AddText(ImVec2(bb.Max.x + style.ItemInnerSpacing.x, label_y_pos),
                               ImGui::GetColorU32(ImGuiCol_Text), label, end);
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
                              ImVec4(0.169f, 0.169f, 0.176f, current_alpha * 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border,
                              ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z,
                                     current_alpha * 0.45f));

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
                                             current_alpha * 0.22f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                      ImVec4(1.0f, 1.0f, 1.0f, current_alpha * 0.08f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                                      ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z,
                                             current_alpha * 0.34f));

                ImVec2 item_top = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##item", is_selected, 0, ImVec2(0, item_height)))
                {
                    *current_item = i;
                    value_changed = true;
                    pendingClose = true;
                }

                // Texto via drawlist: no se toca el cursor del layout
                // (SetCursorPos con posiciones post-item dispara el check
                // de boundaries de ImGui 1.92).
                ImVec2 item_text_size = ImGui::CalcTextSize(items[i].c_str());
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(item_top.x + style.FramePadding.x,
                           item_top.y + std::floor((item_height - item_text_size.y) * 0.5f)),
                    ImGui::GetColorU32(ImGuiCol_Text), items[i].c_str());

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

// ============================ CSlider (WinUI) ============================

static bool SliderWinUI(const char* label, float* v, float v_min, float v_max,
                        const char* format, bool is_int)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    const float width = ImGui::CalcItemWidth();
    const float h = 20.0f;
    const ImVec2 pos = window->DC.CursorPos;
    const float right_edge = pos.x + ImGui::GetContentRegionAvail().x;
    const ImRect bb(pos, ImVec2(pos.x + width, pos.y + h));

    ImGui::ItemSize(bb, 0.0f);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered = false;
    bool held = false;
    ImGui::ButtonBehavior(bb, id, &hovered, &held);

    bool value_changed = false;
    if (held)
    {
        float t = (g.IO.MousePos.x - bb.Min.x) / (bb.Max.x - bb.Min.x);
        if (t < 0.0f)
            t = 0.0f;
        if (t > 1.0f)
            t = 1.0f;
        float nv = v_min + t * (v_max - v_min);
        if (is_int)
            nv = ImTrunc(nv + 0.5f);
        if (nv != *v)
        {
            *v = nv;
            value_changed = true;
        }
    }

    // Animacion de hover: el thumb crece suavemente.
    static std::map<ImGuiID, float> hover_anim;
    float& ha = hover_anim[id];
    ha += (((hovered || held) ? 1.0f : 0.0f) - ha) * ImMin(g.IO.DeltaTime * 12.0f, 1.0f);
    if (ha < 0.001f && !(hovered || held))
        hover_anim.erase(id);

    const float range = v_max - v_min;
    float t = (range > 0.0f) ? (*v - v_min) / range : 0.0f;
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;

    ImDrawList* dl = window->DrawList;
    const float cy = (bb.Min.y + bb.Max.y) * 0.5f;
    const float thumb_r = 6.5f + ha * 1.5f + (held ? 0.8f : 0.0f);
    const float track_min = bb.Min.x + thumb_r + 2.0f;
    const float track_max = bb.Max.x - thumb_r - 2.0f;
    const float thumb_x = track_min + t * (track_max - track_min);

    // Pista inactiva: blanco 11%.
    dl->AddRectFilled(ImVec2(track_min, cy - 2.5f), ImVec2(track_max, cy + 2.5f),
                      IM_COL32(255, 255, 255, 28), 2.5f);
    // Pista activa: acento WinUI.
    dl->AddRectFilled(ImVec2(track_min, cy - 2.5f), ImVec2(thumb_x, cy + 2.5f),
                      accentU32(0.80f + 0.20f * ha), 2.5f);
    // Thumb blanco.
    dl->AddCircleFilled(ImVec2(thumb_x, cy), thumb_r, IM_COL32(244, 245, 247, 255), 16);

    // Etiqueta via drawlist, valor alineado a la derecha (estilo settings de WinUI).
    if (label && *label)
    {
        const char* end = labelEnd(label);
        if (end > label)
        {
            ImVec2 label_size = ImGui::CalcTextSize(label, end);
            float label_y_pos = bb.Min.y + std::floor((h - label_size.y) * 0.5f);
            dl->AddText(ImVec2(bb.Max.x + style.ItemInnerSpacing.x, label_y_pos),
                        ImGui::GetColorU32(ImGuiCol_Text), label, end);
        }
    }

    char buf[96];
    if (is_int)
        snprintf(buf, sizeof(buf), format, (int)*v);
    else
        snprintf(buf, sizeof(buf), format, *v);
    ImVec2 ts = ImGui::CalcTextSize(buf);
    dl->AddText(ImVec2(right_edge - ts.x, bb.Min.y + std::floor((h - ts.y) * 0.5f)),
                IM_COL32(242, 243, 246, 255), buf);

    return value_changed;
}

bool CSlider(const char* label, float* v, float v_min, float v_max, const char* format)
{
    return SliderWinUI(label, v, v_min, v_max, format, false);
}

bool CSlider(const char* label, int* v, int v_min, int v_max, const char* format)
{
    float fv = (float)*v;
    if (!SliderWinUI(label, &fv, (float)v_min, (float)v_max, format, true))
        return false;
    int iv = (int)(fv >= 0.0f ? fv + 0.5f : fv - 0.5f);
    if (iv != *v)
    {
        *v = iv;
        return true;
    }
    return false;
}

}
