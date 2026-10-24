#include "RiseWidgets.hpp"
#include "UIColors.hpp"
#include "sRise.hpp"

#include "Framework/ColorUtils.hpp"
#include "Framework/Math.hpp"
#include "GUI/Theme.hpp"
#include "Modules/impl/Interface/Interface.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

namespace mc::rw {

namespace {

// Geometria base de Rise (valores originales a escala 1.0). El factor `S`
// escala todo segun el chrome de sRise (base x2 * UI Scale).
constexpr float kRowH = 18.0f;
constexpr float kSliderRowH = 20.0f;

inline float S()
{
    return gui::sRise::uiScale();
}

inline float sc(float v)
{
    return v * S();
}

// Animaciones por ID (toggle dot, slider thumb)
std::map<ImGuiID, float>& animMap()
{
    static std::map<ImGuiID, float> m;
    return m;
}

void cleanLabel(const char* label, char* out, size_t cap)
{
    snprintf(out, cap, "%s", label);
    char* h = strchr(out, '#');
    if (h)
        *h = '\0';
}

} // namespace

// ============================================================================
// BooleanValueComponent de Rise 6.9.5
// ============================================================================
bool Toggle(const char* label, bool* v)
{
    char name[64];
    cleanLabel(label, name, sizeof(name));

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    ImDrawList* d = window->DrawList;
    ImFont* font = g.Font;
    float fs = g.FontSize;
    float dt = g.IO.DeltaTime;

    ImVec2 p = window->DC.CursorPos;
    float availW = ImGui::GetContentRegionAvail().x;
    if (availW < 60.0f)
        availW = 60.0f;

    // Ancho del CONTENIDO (label + punto), igual que el hitbox del
    // BooleanValueComponent de Rise (nameWidth + tag + 3). No consumir toda
    // la fila hace que SameLine() funcione: varios toggles pueden compartir
    // fila (p.ej. los checkboxes de minerales de BlockESP) sin salirse del
    // drawer.
    ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, name);
    float contentW = sc(4.0f) + ts.x + sc(8.0f) + sc(3.0f) + sc(4.0f);
    if (contentW < 60.0f)
        contentW = 60.0f;
    if (contentW > availW)
        contentW = availW;

    ImRect bb(p, ImVec2(p.x + contentW, p.y + sc(kRowH)));
    const ImGuiID id = window->GetID(label);

    ImGui::ItemSize(bb, 0.0f);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered = ImGui::IsItemHovered();
    bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    if (clicked)
    {
        *v = !*v;
    }

    // Animación de tamaño del punto (0.0f a 5.0f como axp de Rise)
    float targetDot = *v ? sc(5.5f) : 0.0f;
    float& dotAnim = animMap()[id];
    dotAnim += (targetDot - dotAnim) * clampf(dt * 16.0f, 0.0f, 1.0f);

    // Texto de la configuración con tipografía secondary text de Rise
    float textY = p.y + (sc(kRowH) - ts.y) * 0.5f;
    ImU32 textCol = hovered ? gui::sRise::colors::text() : gui::sRise::colors::secondaryText();
    d->AddText(font, fs, ImVec2(p.x + sc(4.0f), textY), textCol, name);

    // Indicador de punto justo al lado del texto (Rise BooleanValueComponent)
    float dotCenterX = p.x + sc(4.0f) + ts.x + sc(8.0f);
    float dotCenterY = p.y + sc(kRowH) * 0.5f;

    // Track exterior (UIColors.BACKGROUND)
    d->AddCircleFilled(ImVec2(dotCenterX, dotCenterY), sc(3.0f),
                       gui::sRise::colors::background(), 16);

    // Punto interior lleno con el color de acento
    if (dotAnim > 0.05f)
    {
        d->AddCircleFilled(ImVec2(dotCenterX, dotCenterY), dotAnim * 0.5f,
                           theme::AccentU32(), 16);
    }

    return clicked;
}

// ============================================================================
// NumberValueComponent de Rise 6.9.5 (Slider)
// ============================================================================
bool Slider(const char* label, float* v, float mn, float mx, const char* fmt)
{
    char name[64];
    cleanLabel(label, name, sizeof(name));

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    ImDrawList* d = window->DrawList;
    ImFont* font = g.Font;
    float fs = g.FontSize;
    float dt = g.IO.DeltaTime;

    ImVec2 p = window->DC.CursorPos;
    float availW = ImGui::GetContentRegionAvail().x;
    if (availW < 80.0f)
        availW = 80.0f;

    ImRect bb(p, ImVec2(p.x + availW, p.y + sc(kSliderRowH)));
    const ImGuiID id = window->GetID(label);

    ImGui::ItemSize(bb, 0.0f);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    // Texto de la propiedad
    ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, name);
    float textY = p.y + (sc(kSliderRowH) - ts.y) * 0.5f;
    d->AddText(font, fs, ImVec2(p.x + sc(4.0f), textY),
               gui::sRise::colors::secondaryText(), name);

    // Geometría de la pista (Rise NumberValueComponent: track width 100px)
    float trackX = p.x + sc(4.0f) + ts.x + sc(10.0f);
    float trackW = sc(92.0f);
    float trackH = sc(2.0f);
    float trackY = p.y + (sc(kSliderRowH) - trackH) * 0.5f;

    // Pista de slider arrastrable
    ImRect trackHit(ImVec2(trackX - sc(6.0f), p.y),
                    ImVec2(trackX + trackW + sc(6.0f), p.y + sc(kSliderRowH)));
    bool isHovered = ImGui::IsMouseHoveringRect(trackHit.Min, trackHit.Max);

    static ImGuiID activeSliderId = 0;
    bool isDragging = (activeSliderId == id);

    if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        activeSliderId = id;
        isDragging = true;
    }
    if (isDragging && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        activeSliderId = 0;
        isDragging = false;
    }

    bool changed = false;
    if (isDragging)
    {
        float mouseX = g.IO.MousePos.x;
        float newFrac = clampf((mouseX - trackX) / trackW, 0.0f, 1.0f);
        float newVal = mn + (mx - mn) * newFrac;
        if (newVal != *v)
        {
            *v = newVal;
            changed = true;
        }
    }

    // Porcentaje actual del slider
    float pct = (mx != mn) ? clampf((*v - mn) / (mx - mn), 0.0f, 1.0f) : 0.0f;

    // Animación suave del thumb (azl de Rise)
    float& thumbAnim = animMap()[id];
    thumbAnim += (pct - thumbAnim) * clampf(dt * 20.0f, 0.0f, 1.0f);

    // Pista de fondo (UIColors.BACKGROUND)
    d->AddRectFilled(ImVec2(trackX, trackY), ImVec2(trackX + trackW, trackY + trackH),
                     gui::sRise::colors::background(), sc(1.0f));

    // Pista activa con el color de acento
    if (thumbAnim > 0.001f)
    {
        d->AddRectFilled(ImVec2(trackX, trackY),
                         ImVec2(trackX + thumbAnim * trackW, trackY + trackH),
                         theme::AccentU32(), sc(1.0f));
    }

    // Thumb: punto circular con radio 2.5 (5x5 px) en color de acento
    float thumbCenterX = trackX + thumbAnim * trackW;
    float thumbCenterY = p.y + sc(kSliderRowH) * 0.5f;
    d->AddCircleFilled(ImVec2(thumbCenterX, thumbCenterY), sc(2.8f), theme::AccentU32(), 16);

    // Valor formateado a la derecha del slider (TextBox de Rise).
    // OJO: los formatos enteros (%d/%i, los que usa SliderInt) esperan un int
    // en snprintf. Pasar el float es UB: el texto muestra 0 o basura que
    // cambia al arrastrar. Convertimos al entero mas cercano antes.
    if (!fmt)
        fmt = "%.2f";
    char valBuf[32];
    if (strstr(fmt, "%d") != nullptr || strstr(fmt, "%i") != nullptr)
    {
        int iv = (int)(*v >= 0.0f ? *v + 0.5f : *v - 0.5f);
        snprintf(valBuf, sizeof(valBuf), fmt, iv);
    }
    else
    {
        snprintf(valBuf, sizeof(valBuf), fmt, *v);
    }
    ImVec2 valTs = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, valBuf);
    float valX = trackX + trackW + sc(8.0f);
    float valY = p.y + (sc(kSliderRowH) - valTs.y) * 0.5f;
    d->AddText(font, fs, ImVec2(valX, valY),
               gui::sRise::colors::secondaryText(), valBuf);

    return changed;
}

bool SliderInt(const char* label, int* v, int mn, int mx, const char* fmt)
{
    float fv = (float)*v;
    if (Slider(label, &fv, (float)mn, (float)mx, fmt))
    {
        int iv = (int)(fv >= 0.0f ? fv + 0.5f : fv - 0.5f);
        if (iv != *v)
        {
            *v = iv;
            return true;
        }
    }
    return false;
}

// ============================================================================
// ModeValueComponent / ListValueComponent de Rise 6.9.5
// ============================================================================
bool Combo(const char* label, int* current, const char* const items[], int count)
{
    char name[64];
    cleanLabel(label, name, sizeof(name));

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems || count <= 0)
        return false;

    ImGuiContext& g = *GImGui;
    ImDrawList* d = window->DrawList;
    ImFont* font = g.Font;
    float fs = g.FontSize;

    ImVec2 p = window->DC.CursorPos;
    float availW = ImGui::GetContentRegionAvail().x;
    if (availW < 60.0f)
        availW = 60.0f;

    // Ancho del CONTENIDO ("Name: mode"), como el hitbox del
    // ModeValueComponent de Rise. No consumir toda la fila hace que
    // SameLine() funcione (p.ej. NameProtect: From + To en la misma linea).
    char labelWithColon[80];
    snprintf(labelWithColon, sizeof(labelWithColon), "%s:", name);
    ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, labelWithColon);
    int curIdx = (*current >= 0 && *current < count) ? *current : 0;
    const char* curMode = items[curIdx];
    ImVec2 tsMode = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, curMode);
    float contentW = sc(4.0f) + ts.x + sc(4.0f) + tsMode.x + sc(4.0f);
    if (contentW < 60.0f)
        contentW = 60.0f;
    if (contentW > availW)
        contentW = availW;

    ImRect bb(p, ImVec2(p.x + contentW, p.y + sc(kRowH)));
    const ImGuiID id = window->GetID(label);

    ImGui::ItemSize(bb, 0.0f);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered = ImGui::IsItemHovered();
    bool leftClick = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool rightClick = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    bool changed = false;
    // Click izquierdo = siguiente modo, Click derecho = modo anterior (igual que Rise)
    if (leftClick)
    {
        *current = (*current + 1) % count;
        changed = true;
    }
    else if (rightClick)
    {
        *current = (*current - 1 + count) % count;
        changed = true;
    }

    // Texto de la propiedad con dos puntos: "Mode:"
    float textY = p.y + (sc(kRowH) - ts.y) * 0.5f;

    d->AddText(font, fs, ImVec2(p.x + sc(4.0f), textY),
               gui::sRise::colors::secondaryText(), labelWithColon);

    // Modo seleccionado a continuación del label en color de acento
    float modeX = p.x + sc(4.0f) + ts.x + sc(4.0f);
    ImU32 modeCol = hovered ? theme::AccentU32() : theme::AccentU32(0.90f);
    d->AddText(font, fs, ImVec2(modeX, textY), modeCol, curMode);

    return changed;
}

// ============================================================================
// StringValueComponent de Rise 6.9.5
// ============================================================================
bool TextInput(const char* label, char* buf, size_t bufsize)
{
    char name[64];
    cleanLabel(label, name, sizeof(name));

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    ImDrawList* d = window->DrawList;
    ImFont* font = g.Font;
    float fs = g.FontSize;

    ImVec2 p = window->DC.CursorPos;
    float availW = ImGui::GetContentRegionAvail().x;
    if (availW < 80.0f)
        availW = 80.0f;

    // Etiqueta arriba
    ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, name);
    d->AddText(font, fs, ImVec2(p.x + sc(4.0f), p.y + sc(2.0f)),
               gui::sRise::colors::secondaryText(), name);

    // Caja de texto debajo (Rise StringValueComponent)
    float inputH = sc(22.0f);
    float inputW = ImMin(availW - sc(8.0f), sc(230.5f));
    ImVec2 inputPos(p.x + sc(4.0f), p.y + ts.y + sc(4.0f));

    // Reservar el area completa del componente con un item (ItemSize), como
    // los demas widgets de Rise. Solo usar SetCursorScreenPos para "avanzar"
    // al final dispara el assert de ImGui 1.92+:
    // "SetCursorPos()/SetCursorScreenPos() to extend window/parent boundaries".
    float totalH = ts.y + sc(4.0f) + inputH + sc(2.0f);
    ImGui::ItemSize(ImVec2(availW, totalH), 0.0f);

    ImGui::SetCursorScreenPos(inputPos);
    ImGui::PushItemWidth(inputW);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(sc(6.0f), sc(3.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, sc(4.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.09f, 0.10f, 0.13f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.13f, 0.15f, 0.19f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.16f, 0.18f, 0.23f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.10f));

    bool changed = ImGui::InputText(label, buf, bufsize);

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
    ImGui::PopItemWidth();

    // Cursor al final del componente + item final. El Dummy(0,0) es
    // imprescindible: dejar un SetCursorScreenPos como ultima operacion
    // dispara el assert "extend window/parent boundaries" de ImGui 1.92+,
    // porque el cursor post-item queda ItemSpacing por encima de CursorMaxPos
    // (truncado a entero) aunque el area se haya reservado con ItemSize antes.
    // Con el Dummy el item limpia IsSetPos y deja el cursor exactamente en la
    // posicion natural post-item (p.y + totalH + ItemSpacing), igual que los
    // demas widgets de Rise.
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + totalH));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));

    return changed;
}

// ============================================================================
// ColorValueComponent de Rise 6.9.5
// ============================================================================
bool Color(const char* label, float* col, int components)
{
    char name[64];
    cleanLabel(label, name, sizeof(name));

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    ImDrawList* d = window->DrawList;
    ImFont* font = g.Font;
    float fs = g.FontSize;

    ImVec2 p = window->DC.CursorPos;
    float availW = ImGui::GetContentRegionAvail().x;
    if (availW < 60.0f)
        availW = 60.0f;

    // Ancho del CONTENIDO (label + swatch) para que SameLine() componga.
    ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, name);
    float swatchW = sc(16.0f);
    float contentW = sc(4.0f) + ts.x + sc(8.0f) + swatchW + sc(4.0f);
    if (contentW < 60.0f)
        contentW = 60.0f;
    if (contentW > availW)
        contentW = availW;

    ImRect bb(p, ImVec2(p.x + contentW, p.y + sc(kRowH)));
    const ImGuiID id = window->GetID(label);

    ImGui::ItemSize(bb, 0.0f);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered = ImGui::IsItemHovered();
    char popupId[80];
    snprintf(popupId, sizeof(popupId), "##risecol_%s", label);

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        ImGui::OpenPopup(popupId);
    }

    // Nombre de la propiedad
    float textY = p.y + (sc(kRowH) - ts.y) * 0.5f;
    d->AddText(font, fs, ImVec2(p.x + sc(4.0f), textY),
               gui::sRise::colors::secondaryText(), name);

    // Muestra de color (Rise ColorValueComponent: 15.0x7.0 rounded rect con radio 2.5)
    float swatchH = sc(8.0f);
    float swatchX = p.x + sc(4.0f) + ts.x + sc(8.0f);
    float swatchY = p.y + (sc(kRowH) - swatchH) * 0.5f;

    ImU32 colU32 = ImGui::ColorConvertFloat4ToU32(
        ImVec4(col[0], col[1], col[2], components > 3 ? col[3] : 1.0f));

    d->AddRectFilled(ImVec2(swatchX, swatchY), ImVec2(swatchX + swatchW, swatchY + swatchH),
                     colU32, sc(2.5f));
    d->AddRect(ImVec2(swatchX, swatchY), ImVec2(swatchX + swatchW, swatchY + swatchH),
               IM_COL32(255, 255, 255, 40), sc(2.5f), 0, sc(1.0f));

    bool changed = false;
    ImGui::SetNextWindowPos(ImVec2(swatchX, swatchY + swatchH + sc(4.0f)), ImGuiCond_Appearing);
    if (ImGui::BeginPopup(popupId))
    {
        ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoSidePreview;
        if (components <= 3)
            flags |= ImGuiColorEditFlags_NoAlpha;
        changed = ImGui::ColorPicker4(label, col, flags);
        ImGui::EndPopup();
    }

    return changed;
}

} // namespace mc::rw
