#include "Widgets.hpp"

#include "Framework/ColorUtils.hpp"
#include "Framework/SolsticeWidgets.hpp"
#include "Modules/impl/Interface/Style/sRise/RiseWidgets.hpp"
#include "GUI/Theme.hpp"
#include "Modules/impl/Interface/Interface.hpp"
#include "Render/DX11.hpp"
#include "Render/D2D/D2D.hpp"
#include "Render/BlurHelper/BlurHelper.hpp"

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

// Glow negro solo en los bordes EXTERIORES de una pila de rects (staircase ArrayList).
// Cada borde se dibuja de forma independiente para evitar sangrado en seams interiores:
//   - Lado izquierdo de cada rect: tiras verticales clippeadas a [rMin.y, rMax.y].
//   - Tope del primer rect y base del ultimo rect: tiras horizontales.
// rects: ordenados de arriba a abajo, cada elemento es {min, max}.
void GlowBorder(ImDrawList* d,
                const std::vector<std::pair<ImVec2, ImVec2>>& rects,
                float strength, float spread)
{
    if (rects.empty() || strength <= 0.f || spread <= 0.f)
        return;

    const int layers = 9;
    for (int i = 0; i < layers; ++i)
    {
        float t   = (float)i / (float)(layers - 1);
        float pad = spread * t;
        float f   = 1.f - t;
        f = f * f * f;
        int a = (int)(strength * f);
        if (a <= 0)
            continue;
        ImU32 col = IM_COL32(0, 0, 0, a);

        // Lado izquierdo de cada fila: tira vertical clippeada a la altura de ESA fila.
        // pad=0 en i=0 → el rect tiene ancho 0 (invisible), y crece hacia la izquierda.
        // Esto garantiza que NO hay sangrado vertical entre filas adyacentes.
        for (const auto& [rMin, rMax] : rects)
            d->AddRectFilled(ImVec2(rMin.x - pad, rMin.y),
                             ImVec2(rMin.x,       rMax.y), col);

        // Borde superior del bloque completo (encima del primer rect).
        {
            const ImVec2& fMin = rects.front().first;
            const ImVec2& fMax = rects.front().second;
            d->AddRectFilled(ImVec2(fMin.x - pad, fMin.y - pad),
                             ImVec2(fMax.x,       fMin.y), col);
        }

        // Borde inferior del bloque completo (debajo del ultimo rect).
        {
            const ImVec2& lMin = rects.back().first;
            const ImVec2& lMax = rects.back().second;
            d->AddRectFilled(ImVec2(lMin.x - pad, lMax.y),
                             ImVec2(lMax.x,       lMax.y + pad), col);
        }
    }
}


// Panel acrilico: el vidrio lo compone el BlurHelper en el backbuffer con el
// shader Glass (rounded-rect SDF + refraccion + luz de borde), igual que el
// menu. Aqui solo se registra el rect para el pase de blur y se pinta el
// scrim tenue + borde por encima. Sin blur disponible: tarjeta solida.
void AcrylicPanel(ImDrawList* d, const ImVec2& min, const ImVec2& max,
                  float rounding, float alpha, float sw, float sh,
                  float shadowStrength, float shadowSpread, bool drawBorder)
{
    if (alpha <= 0.01f)
        return;

    SoftShadow(d, min, max, rounding, shadowStrength * alpha, shadowSpread);

    // Modo Solstice: blur con Direct2D (callback en el draw list).
#ifdef _MSC_VER
    if (gui::blurMode() == 1)
    {
        if (D2D::addBlur(d, 10.0f, ImVec4(min.x, min.y, max.x, max.y), rounding))
        {
            d->AddRectFilled(min, max, IM_COL32(16, 20, 28, (int)(66.0f * alpha)), rounding);
            if (drawBorder)
                d->AddRect(min, max, IM_COL32(255, 255, 255, (int)(22.0f * alpha)), rounding, 0, 1.0f);
            return;
        }
    }
#endif

    // Modo Native: panel acrilico via BlurHelper (DX11).
    const bool haveGlass = blur::ready() && blur::srv();
    if (haveGlass)
        blur::addAcrylicRect(min.x, min.y, max.x, max.y, rounding, alpha);
    else
        d->AddRectFilled(min, max, IM_COL32(31, 31, 33, (int)(200.0f * alpha)), rounding);

    d->AddRectFilled(min, max, IM_COL32(16, 20, 28, (int)(66.0f * alpha)), rounding);
    if (drawBorder)
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
    // Estilo Solstice: combo custom (sw::Combo). Se reconstruye el array
    // de const char* para la version custom.
    if (gui::style() == 1)
    {
        std::vector<const char*> raw;
        raw.reserve(items.size());
        for (const std::string& s : items)
            raw.push_back(s.c_str());
        return sw::Combo(label, current_item, raw.data(), (int)raw.size());
    }
    if (gui::style() == 2)
    {
        std::vector<const char*> raw;
        raw.reserve(items.size());
        for (const std::string& s : items)
            raw.push_back(s.c_str());
        return rw::Combo(label, current_item, raw.data(), (int)raw.size());
    }
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

    char buf[96];
    if (is_int)
        snprintf(buf, sizeof(buf), format, (int)*v);
    else
        snprintf(buf, sizeof(buf), format, *v);
    ImVec2 ts = ImGui::CalcTextSize(buf);

    // El valor es un InputBox siempre editable (click directo sobre el).
    // CTRL+click / doble click en la pista lo enfoca como atajo.
    struct ValBox
    {
        char buf[32] = {};
        bool focused = false;
        bool wantFocus = false;
    };
    static std::map<ImGuiID, ValBox> val_boxes;
    ValBox& vb = val_boxes[id];

    float fieldW = ImMax(ts.x + 22.0f, 58.0f);
    ImRect valRect(right_edge - fieldW, bb.Min.y, right_edge, bb.Max.y);

    if (!vb.focused && !vb.wantFocus)
        snprintf(vb.buf, sizeof(vb.buf), is_int ? "%d" : "%.4g", is_int ? (int)*v : *v);

    // Animacion de hover.
    static std::map<ImGuiID, float> hover_anim;
    float& ha = hover_anim[id];
    ha += (((hovered || held) ? 1.0f : 0.0f) - ha) * ImMin(g.IO.DeltaTime * 12.0f, 1.0f);
    if (ha < 0.001f && !(hovered || held))
        hover_anim.erase(id);

    // Coordenadas de la pista (necesarias para el drag).
    const float range = v_max - v_min;
    ImDrawList* dl = window->DrawList;
    const float cy = (bb.Min.y + bb.Max.y) * 0.5f;
    const float thumb_r = 6.5f + ha * 1.5f + (held ? 0.8f : 0.0f);
    const float track_min = bb.Min.x + thumb_r + 2.0f;
    const float track_max = valRect.Min.x - thumb_r - 4.0f;

    bool value_changed = false;
    // Drag: calcula t sobre el rango REAL de la pista, no todo el bb.
    // Solo activo cuando el inputbox no tiene el foco.
    if (!vb.focused && held)
    {
        float rawT = (g.IO.MousePos.x - track_min) / (track_max - track_min);
        if (rawT < 0.0f) rawT = 0.0f;
        if (rawT > 1.0f) rawT = 1.0f;
        float nv = v_min + rawT * (v_max - v_min);
        if (is_int)
            nv = ImTrunc(nv + 0.5f);
        if (nv != *v)
        {
            *v = nv;
            value_changed = true;
        }
    }

    // Activar inputbox con CTRL+click o doble-click fuera del inputbox.
    if (!vb.focused && !vb.wantFocus && hovered &&
        !valRect.Contains(g.IO.MousePos) &&
        (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) ||
         (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && g.IO.KeyCtrl)))
    {
        snprintf(vb.buf, sizeof(vb.buf), is_int ? "%d" : "%.4g", is_int ? (int)*v : *v);
        vb.wantFocus = true;
    }

    float t = (range > 0.0f) ? (*v - v_min) / range : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    // Pista inactiva.
    dl->AddRectFilled(ImVec2(track_min, cy - 2.5f), ImVec2(track_max, cy + 2.5f),
                      IM_COL32(255, 255, 255, 28), 2.5f);

    // Etiqueta.
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

    // InputBox del valor.
    {
        ImVec2 savedPos = ImGui::GetCursorScreenPos();

        if (vb.wantFocus)
            ImGui::SetKeyboardFocusHere(0);
        ImGui::SetNextItemWidth(fieldW);
        ImGui::SetCursorScreenPos(ImVec2(right_edge - fieldW, bb.Min.y - 1.0f));
        ImGui::PushID((int)id);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

        // Cuando no esta enfocado, el input box es invisible para no
        // robar los eventos del slider (hover/click del drag).
        if (!vb.focused && !vb.wantFocus)
        {
            ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_Text,           ImVec4(0,0,0,0));
        }

        bool done = ImGui::InputText(
            "##slider_val", vb.buf, sizeof(vb.buf),
            ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CharsDecimal |
                ImGuiInputTextFlags_EnterReturnsTrue);
        bool focused_now = ImGui::IsItemFocused();
        bool escaped = focused_now && ImGui::IsKeyPressed(ImGuiKey_Escape, false);

        if (!vb.focused && !vb.wantFocus)
            ImGui::PopStyleColor(4);

        ImGui::PopStyleVar(2);
        ImGui::PopID();
        ImGui::SetCursorScreenPos(savedPos);

        if (vb.wantFocus)
            vb.focused = true;
        vb.wantFocus = false;

        if (done || escaped || (vb.focused && !focused_now))
        {
            if (!escaped)
            {
                float nv = (float)atof(vb.buf);
                if (is_int) nv = (float)(int)nv;
                if (nv < v_min) nv = v_min;
                if (nv != *v)
                {
                    *v = nv;
                    value_changed = true;
                }
            }
            vb.focused = false;
            snprintf(vb.buf, sizeof(vb.buf), is_int ? "%d" : "%.4g", is_int ? (int)*v : *v);
        }
        else
        {
            vb.focused = focused_now;
        }
    }

    // Dibujar el valor visible cuando el inputbox no esta activo.
    if (!vb.focused)
    {
        char disp[64];
        if (is_int) snprintf(disp, sizeof(disp), format, (int)*v);
        else        snprintf(disp, sizeof(disp), format, *v);
        ImVec2 tsz = ImGui::CalcTextSize(disp);
        float vx = valRect.Min.x + (valRect.GetWidth() - tsz.x) * 0.5f;
        float vy = bb.Min.y + (h - tsz.y) * 0.5f;
        dl->AddText(ImVec2(vx, vy), ImGui::GetColorU32(ImGuiCol_TextDisabled), disp);
    }

    // Thumb y pista activa.
    t = (range > 0.0f) ? (*v - v_min) / range : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    const float thumb_x = track_min + t * (track_max - track_min);
    dl->AddRectFilled(ImVec2(track_min, cy - 2.5f), ImVec2(ImMin(thumb_x, track_max), cy + 2.5f),
                      accentU32(0.80f + 0.20f * ha), 2.5f);
    dl->AddCircleFilled(ImVec2(ImMin(thumb_x, track_max), cy), thumb_r,
                       IM_COL32(244, 245, 247, 255), 16);

    return value_changed;
}

bool Checkbox(const char* label, bool* v)
{
    if (gui::style() == 1)
        return sw::Toggle(label, v);
    if (gui::style() == 2)
        return rw::Toggle(label, v);
    return ImGui::Checkbox(label, v);
}

bool InputText(const char* label, char* buf, size_t size)
{
    if (gui::style() == 1)
        return sw::TextInput(label, buf, size);
    if (gui::style() == 2)
        return rw::TextInput(label, buf, size);
    return ImGui::InputText(label, buf, size);
}

bool ColorEdit3(const char* label, float* col3, ImGuiColorEditFlags flags)
{
    if (gui::style() == 1)
        return sw::Color(label, col3, 3);
    if (gui::style() == 2)
        return rw::Color(label, col3, 3);
    return ImGui::ColorEdit3(label, col3, flags);
}

bool ColorEdit4(const char* label, float* col4, ImGuiColorEditFlags flags)
{
    if (gui::style() == 1)
        return sw::Color(label, col4, 4);
    if (gui::style() == 2)
        return rw::Color(label, col4, 4);
    return ImGui::ColorEdit4(label, col4, flags);
}

bool CSlider(const char* label, float* v, float v_min, float v_max, const char* format)
{
    if (gui::style() == 1)
        return sw::Slider(label, v, v_min, v_max, format);
    if (gui::style() == 2)
        return rw::Slider(label, v, v_min, v_max, format);
    return SliderWinUI(label, v, v_min, v_max, format, false);
}

bool CSlider(const char* label, int* v, int v_min, int v_max, const char* format)
{
    if (gui::style() == 1)
        return sw::SliderInt(label, v, v_min, v_max, format);
    if (gui::style() == 2)
        return rw::SliderInt(label, v, v_min, v_max, format);
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
