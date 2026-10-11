#include "SolsticeWidgets.hpp"

#include "ColorUtils.hpp"
#include "ImRenderUtils.hpp"
#include "Modules/impl/Interface/Interface.hpp"

#include <imgui_internal.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

namespace mc::sw {

namespace {

constexpr float kRowH = 26.0f;

// Animaciones por item (clave = ImGuiID del label en su ventana).
std::map<ImGuiID, float>& animMap()
{
    static std::map<ImGuiID, float> m;
    return m;
}

float tickAnim(ImGuiID id, float target, float speed = 10.0f)
{
    float& a = animMap()[id];
    a += (target - a) * ImGui::GetIO().DeltaTime * speed;
    if (a < 0.0f)
        a = 0.0f;
    if (a > 1.0f)
        a = 1.0f;
    return a;
}

// Nombre visible del label (sin la parte ##id).
void displayLabel(const char* label, char* out, size_t cap)
{
    snprintf(out, cap, "%s", label);
    char* h = strchr(out, '#');
    if (h)
        *h = '\0';
}

// Fila base: rect + fill oscuro + layout reservado con Dummy. Devuelve el
// rect en coords de pantalla.
ImVec4 beginRow(const char* label, float h = kRowH)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    if (w < 40.0f)
        w = 40.0f;

    ImVec4 r(p.x, p.y, p.x + w, p.y + h);
    window->DrawList->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.z, r.w),
                                    IM_COL32(30, 30, 30, 235), 4.0f);
    ImGui::Dummy(ImVec2(w, h));
    if (label)
        ImGui::PushID(label);
    return r;
}

void endRow(const char* label)
{
    if (label)
        ImGui::PopID();
}

// drawCheckMark de Solstice: dos segmentos en "V" desde el punto.
void drawCheckMark(ImDrawList* d, const ImVec2& pos, float size, ImU32 col, float alpha)
{
    float th = 2.0f * size;
    ImU32 c = (col & 0x00FFFFFFu) | ((unsigned)((int)(alpha * 255.0f) << 24));
    ImVec2 end1(pos.x + 3.0f * size, pos.y + 3.0f * size);
    ImVec2 end2(pos.x + 7.0f * size, pos.y - 3.0f * size);
    d->AddLine(pos, end1, c, th);
    d->AddLine(end1, end2, c, th);
}

// fillShadowCircle aproximado: capas de circulos con caida.
void drawGlowCircle(ImDrawList* d, const ImVec2& center, float radius, ImU32 col, float alpha)
{
    for (int i = 0; i < 5; ++i)
    {
        float t = (float)i / 4.0f;
        float rr = radius + t * 5.0f;
        float a = alpha * (1.0f - t) * (1.0f - t);
        d->AddCircleFilled(center, rr,
                           (col & 0x00FFFFFFu) | ((unsigned)((int)(a * 255.0f) << 24)));
    }
}

} // namespace

bool Toggle(const char* label, bool* v)
{
    char name[64];
    displayLabel(label, name, sizeof(name));

    ImVec4 r = beginRow(label);
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImGuiID id = ImGui::GetID(label);

    bool changed = false;
    if (ImGui::IsMouseHoveringRect(ImVec2(r.x, r.y), ImVec2(r.z, r.w)) &&
        ImGui::IsMouseClicked(0))
    {
        *v = !*v;
        changed = true;
    }

    float anim = tickAnim(id, *v ? 1.0f : 0.0f);
    ImU32 themed = colors::getThemedColor(r.y * 2.0f, 255);

    // Nombre a la izquierda.
    {
        float fs = 15.0f;
        ImVec2 ts = ImGui::CalcTextSize(name);
        d->AddText(ImVec2(r.x + 6.0f, r.y + (kRowH - ts.y) * 0.5f),
                   IM_COL32(255, 255, 255, 235), name);
    }

    // Zona del toggle a la derecha (booleanRect del original).
    ImVec4 zone(r.z - 23.5f, r.y, r.z - 5.0f, r.w);
    ImVec2 center((zone.x + zone.z) * 0.5f, (zone.y + zone.w) * 0.5f);

    // Glow: lerp de (15,15,15) al color del tema con boolScale.
    {
        float inv = 1.0f - anim;
        ImU32 shadowCol =
            IM_COL32((int)(15 + (((themed >> 0) & 0xFF) - 15) * anim),
                     (int)(15 + (((themed >> 8) & 0xFF) - 15) * anim),
                     (int)(15 + (((themed >> 16) & 0xFF) - 15) * anim), 255);
        (void)inv;
        drawGlowCircle(d, center, 5.0f, shadowCol, 0.55f * anim);
    }

    // Checkmark revelado de izquierda a derecha con el clip del original.
    if (anim > 0.01f)
    {
        float zoneW = zone.z - zone.x;
        d->PushClipRect(ImVec2(zone.x, zone.y),
                        ImVec2(zone.x + zoneW * anim, zone.w), true);
        drawCheckMark(d, ImVec2(center.x - 5.0f, center.y - 1.0f), 1.3f, themed, anim);
        d->PopClipRect();
    }

    endRow(label);
    return changed;
}

bool Slider(const char* label, float* v, float mn, float mx, const char* fmt)
{
    char name[64];
    displayLabel(label, name, sizeof(name));

    ImVec4 r = beginRow(label);
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImGuiID id = ImGui::GetID(label);

    bool changed = false;

    // Nombre a la izquierda, valor a la derecha en el color del tema.
    char val[32];
    snprintf(val, sizeof(val), fmt, *v);
    ImU32 themed = colors::getThemedColor(r.y * 2.0f, 255);
    {
        float fs = 15.0f;
        ImVec2 ts = ImGui::CalcTextSize(name);
        d->AddText(ImVec2(r.x + 6.0f, r.y + 6.0f), IM_COL32(255, 255, 255, 235), name);
        ImVec2 vs = ImGui::CalcTextSize(val);
        d->AddText(ImVec2(r.z - vs.x - 8.0f, r.y + 6.0f), themed, val);
    }

    // Barra en el borde inferior de la fila.
    float barH = 4.0f;
    ImVec4 bar(r.x + 3.0f, r.w - barH - 2.0f, r.z - 3.0f, r.w - 2.0f);
    float frac = (mn != mx) ? (*v - mn) / (mx - mn) : 0.0f;
    if (frac < 0.0f)
        frac = 0.0f;
    if (frac > 1.0f)
        frac = 1.0f;

    d->AddRectFilled(ImVec2(bar.x, bar.y), ImVec2(bar.z, bar.w),
                      IM_COL32(45, 45, 50, 235), barH * 0.5f);
    if (frac > 0.001f)
    {
        renderutils::fillRoundedGradientRectangle(
            ImVec4(bar.x, bar.y, bar.x + (bar.z - bar.x) * frac, bar.w),
            themed, colors::getThemedColor(r.y * 2.0f + 120.0f, 255),
            barH * 0.5f, 1.0f, 1.0f, 0, d);
    }

    // Drag: click/hold sobre la fila fija el valor por la posicion X.
    if (ImGui::IsMouseHoveringRect(ImVec2(r.x, bar.y - 4.0f), ImVec2(r.z, r.w)) &&
        ImGui::IsMouseDown(0))
    {
        float mx_ = ImGui::GetIO().MousePos.x;
        float nf = (mx_ - bar.x) / (bar.z - bar.x);
        if (nf < 0.0f)
            nf = 0.0f;
        if (nf > 1.0f)
            nf = 1.0f;
        float nv = mn + (mx - mn) * nf;
        if (nv != *v)
        {
            *v = nv;
            changed = true;
        }
    }

    endRow(label);
    return changed;
}

bool SliderInt(const char* label, int* v, int mn, int mx, const char* fmt)
{
    char name[64];
    displayLabel(label, name, sizeof(name));

    ImVec4 r = beginRow(label);
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImU32 themed = colors::getThemedColor(r.y * 2.0f, 255);

    bool changed = false;
    char val[32];
    snprintf(val, sizeof(val), fmt, *v);
    {
        d->AddText(ImVec2(r.x + 6.0f, r.y + 6.0f), IM_COL32(255, 255, 255, 235), name);
        ImVec2 vs = ImGui::CalcTextSize(val);
        d->AddText(ImVec2(r.z - vs.x - 8.0f, r.y + 6.0f), themed, val);
    }

    float barH = 4.0f;
    ImVec4 bar(r.x + 3.0f, r.w - barH - 2.0f, r.z - 3.0f, r.w - 2.0f);
    float frac = (mn != mx) ? (float)(*v - mn) / (float)(mx - mn) : 0.0f;
    if (frac < 0.0f)
        frac = 0.0f;
    if (frac > 1.0f)
        frac = 1.0f;

    d->AddRectFilled(ImVec2(bar.x, bar.y), ImVec2(bar.z, bar.w),
                     IM_COL32(45, 45, 50, 235), barH * 0.5f);
    if (frac > 0.001f)
    {
        renderutils::fillRoundedGradientRectangle(
            ImVec4(bar.x, bar.y, bar.x + (bar.z - bar.x) * frac, bar.w),
            themed, colors::getThemedColor(r.y * 2.0f + 120.0f, 255),
            barH * 0.5f, 1.0f, 1.0f, 0, d);
    }

    if (ImGui::IsMouseHoveringRect(ImVec2(r.x, bar.y - 4.0f), ImVec2(r.z, r.w)) &&
        ImGui::IsMouseDown(0))
    {
        float nf = (ImGui::GetIO().MousePos.x - bar.x) / (bar.z - bar.x);
        if (nf < 0.0f)
            nf = 0.0f;
        if (nf > 1.0f)
            nf = 1.0f;
        int nv = mn + (int)((float)(mx - mn) * nf + 0.5f);
        if (nv != *v)
        {
            *v = nv;
            changed = true;
        }
    }

    endRow(label);
    return changed;
}

bool Combo(const char* label, int* current, const char* const items[], int count)
{
    char name[64];
    displayLabel(label, name, sizeof(name));

    ImVec4 r = beginRow(label);
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImGuiID id = ImGui::GetID(label);
    ImU32 themed = colors::getThemedColor(r.y * 2.0f, 255);

    int cur = (*current >= 0 && *current < count) ? *current : 0;

    bool changed = false;
    char popId[80];
    snprintf(popId, sizeof(popId), "##swpop_%s", label);
    if (ImGui::IsMouseHoveringRect(ImVec2(r.x, r.y), ImVec2(r.z, r.w)) &&
        ImGui::IsMouseClicked(0))
        ImGui::OpenPopup(popId);

    // Nombre a la izquierda, valor actual a la derecha en el color del tema.
    {
        d->AddText(ImVec2(r.x + 6.0f, r.y + 6.0f), IM_COL32(255, 255, 255, 235), name);
        const char* val = items[cur];
        ImVec2 vs = ImGui::CalcTextSize(val);
        d->AddText(ImVec2(r.z - vs.x - 8.0f, r.y + 6.0f), themed, val);
    }

    ImGui::SetNextWindowPos(ImVec2(r.x, r.w + 2.0f));
    if (ImGui::BeginPopup(popId))
    {
        for (int i = 0; i < count; ++i)
        {
            bool sel = (i == cur);
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                  ImGui::ColorConvertU32ToFloat4(themed) * ImVec4(1, 1, 1, 0.25f) +
                                      ImVec4(0, 0, 0, 0.75f));
            char itemLabel[96];
            snprintf(itemLabel, sizeof(itemLabel), "%s##it%d", items[i], i);
            if (ImGui::Selectable(itemLabel, sel))
            {
                *current = i;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor(2);
        }
        ImGui::EndPopup();
    }

    endRow(label);
    return changed;
}

bool TextInput(const char* label, char* buf, size_t bufsize)
{
    char name[64];
    displayLabel(label, name, sizeof(name));

    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    if (w < 80.0f)
        w = 80.0f;
    ImDrawList* d = ImGui::GetWindowDrawList();

    d->AddRectFilled(p, ImVec2(p.x + w, p.y + kRowH), IM_COL32(30, 30, 30, 235), 4.0f);
    ImVec2 ts = ImGui::CalcTextSize(name);
    d->AddText(ImVec2(p.x + 6.0f, p.y + (kRowH - ts.y) * 0.5f),
               IM_COL32(255, 255, 255, 235), name);

    float fieldW = w - ts.x - 24.0f;
    if (fieldW > 130.0f)
        fieldW = 130.0f;
    if (fieldW < 40.0f)
        fieldW = 40.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 5.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.16f, 0.19f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.20f, 0.20f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.22f, 0.22f, 0.26f, 1.0f));
    ImGui::Dummy(ImVec2(ts.x + 14.0f, kRowH));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(fieldW);
    bool changed = ImGui::InputText(label, buf, bufsize);
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    return changed;
}

bool Color(const char* label, float* col, int components)
{
    char name[64];
    displayLabel(label, name, sizeof(name));

    ImVec4 r = beginRow(label);
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImU32 themed = colors::getThemedColor(r.y * 2.0f, 255);

    char popId[80];
    snprintf(popId, sizeof(popId), "##swcol_%s", label);
    bool changed = false;

    if (ImGui::IsMouseHoveringRect(ImVec2(r.x, r.y), ImVec2(r.z, r.w)) &&
        ImGui::IsMouseClicked(0))
        ImGui::OpenPopup(popId);

    d->AddText(ImVec2(r.x + 6.0f, r.y + 6.0f), IM_COL32(255, 255, 255, 235), name);

    // Swatch del color actual a la derecha.
    {
        float sz = 16.0f;
        ImVec2 sMin(r.z - sz - 6.0f, r.y + (kRowH - sz) * 0.5f);
        ImU32 cur = ImGui::ColorConvertFloat4ToU32(
            ImVec4(col[0], col[1], col[2], components > 3 ? col[3] : 1.0f));
        d->AddRectFilled(sMin, ImVec2(sMin.x + sz, sMin.y + sz), cur, 4.0f);
        d->AddRect(sMin, ImVec2(sMin.x + sz, sMin.y + sz),
                   (themed & 0x00FFFFFFu) | 0xFF000000u, 4.0f);
    }

    ImGui::SetNextWindowPos(ImVec2(r.x, r.w + 2.0f));
    if (ImGui::BeginPopup(popId))
    {
        ImGuiColorEditFlags flags =
            ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoSidePreview;
        if (components <= 3)
            flags |= ImGuiColorEditFlags_NoAlpha;
        ImGui::ColorPicker4(label, col, flags);
        ImGui::EndPopup();
        changed = true;
    }

    endRow(label);
    return changed;
}

} // namespace
