#include "sSolstice.hpp"

#include "Framework/ColorUtils.hpp"
#include "Framework/ImRenderUtils.hpp"
#include "Framework/Math.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Theme.hpp"
#include "Modules/ModuleManager.hpp"
#include "Modules/impl/Interface/Interface.hpp"
#include "Render/BlurHelper/BlurHelper.hpp"
#include "Render/DX11.hpp"

#include <imgui.h>

#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace mc::gui::sSolstice {

namespace {

// --- Geometria base (como ModernGui) ---
constexpr float kCatW = 210.0f;
constexpr float kCatGap = 14.0f;
constexpr float kHeaderH = 38.0f;
constexpr float kRowH = 26.0f;
constexpr float kViewH = 330.0f;
constexpr float kTopY = 60.0f;

float easeOutExpo(float t)
{
    return (t >= 1.0f) ? 1.0f : 1.0f - powf(2.0f, -10.0f * t);
}

float easeOutElastic(float t)
{
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;
    const float c4 = 2.0f * 3.14159265358979323846f / 3.0f;
    return powf(2.0f, -10.0f * t) * sinf((t * 10.0f - 0.75f) * c4) + 1.0f;
}

float easeOutBack(float t)
{
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    float inv = t - 1.0f;
    return 1.0f + c3 * inv * inv * inv + c1 * inv * inv;
}

// scaleToPoint de Solstice: el rect se encoge hacia el centro de pantalla.
ImVec4 scaleRect(const ImVec4& r, const ImVec2& c, float s)
{
    return ImVec4(c.x + (r.x - c.x) * s, c.y + (r.y - c.y) * s,
                 c.x + (r.z - c.x) * s, c.y + (r.w - c.y) * s);
}

bool containsCI(const std::string& haystack, const std::string& needle)
{
    if (needle.empty())
        return true;
    size_t nl = needle.size();
    if (haystack.size() < nl)
        return false;
    for (size_t i = 0; i + nl <= haystack.size(); ++i)
    {
        size_t j = 0;
        while (j < nl && tolower((unsigned char)haystack[i + j]) == tolower((unsigned char)needle[j]))
            ++j;
        if (j == nl)
            return true;
    }
    return false;
}

struct CatState
{
    bool extended = true;
    float scroll = 0.0f;
    float scrollT = 0.0f;
};

// Estado por modulo: settings abiertos, animaciones y altura cacheada de la
// ventana de settings (para que las filas de abajo se desplazen, como el
// settingsHeight/cAnim de Solstice).
std::map<const Module*, bool>& expandedMap()
{
    static std::map<const Module*, bool> m;
    return m;
}
std::map<const Module*, float>& expandAnimMap()
{
    static std::map<const Module*, float> m;
    return m;
}
std::map<const Module*, float>& settingsHeightMap()
{
    static std::map<const Module*, float> m;
    return m;
}
std::map<const Module*, float>& enableAnimMap()
{
    static std::map<const Module*, float> m;
    return m;
}

} // namespace

void render(float anim)
{
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 screen = io.DisplaySize;
    float dt = io.DeltaTime;
    if (screen.x <= 1.0f || screen.y <= 1.0f)
        return;

    // Como ModernGui de Solstice: los PANELES van en el background drawlist
    // para que las ventanas ImGui (settings, search, popups) queden ENCIMA
    // (orden ImGui: background -> ventanas -> foreground). Tabs, tooltip y
    // hints van en foreground (ClickGui.cpp los dibuja ahi).
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    ImFont* font = dx11::hudFont();
    if (!font)
        font = ImGui::GetFont();
    ImVec2 center(screen.x * 0.5f, screen.y * 0.5f);
    ImVec2 mouse = io.MousePos;

    // --- Porcentaje propio con easing configurable (como ClickGui) ---
    static float pct = 0.0f;
    bool vis = menu::visible();
    float step = dt * gui::solsticeEase() / 10.0f * (vis ? 1.0f : 2.0f);
    pct += vis ? step : -step;
    if (pct < 0.0f)
        pct = 0.0f;
    if (pct > 1.0f)
        pct = 1.0f;

    float inScale;
    if (gui::solsticeAnim() == 0)
    {
        inScale = easeOutExpo(pct);
        if (inScale > 0.996f)
            inScale = 0.996f;
    }
    else
    {
        inScale = vis ? easeOutElastic(pct) : easeOutBack(pct);
    }

    float fade = easeOutExpo(pct);
    (void)anim;
    if (fade < 0.0001f)
        return;

    // Accent vivo del tema (fluye con el tiempo como toda la GUI).
    ImU32 accent = colors::getThemedColor(0.0f, 1.0f);

    // --- Backdrop: dim + blur + gradiente inferior (puerto de ModernGui) ---
    dl->AddRectFilled(ImVec2(0.0f, 0.0f), screen, IM_COL32(0, 0, 0, (int)(97.0f * fade)));
    if (blur::wanted() && blur::ready() && blur::srv())
    {
        float a = clampf(gui::solsticeBlur() / 10.0f, 0.0f, 1.0f);
        dl->AddImage(ImTextureRef((void*)blur::srv()), ImVec2(0.0f, 0.0f), screen,
                     ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                     IM_COL32(255, 255, 255, (int)(255.0f * fade * a)));
    }
    {
        float firstH = screen.y - screen.y / 3.0f;
        firstH = screen.y + (firstH - screen.y) * inScale;
        ImU32 cBot = (accent & 0x00FFFFFFu) | ((unsigned)((int)(0.4f * inScale * 255.0f)) << 24);
        ImU32 cTop = IM_COL32(0, 0, 0, 0);
        dl->AddRectFilledMultiColor(ImVec2(0.0f, firstH), ImVec2(screen.x, screen.y),
                                    cTop, cTop, cBot, cBot);
    }

    // --- Tabs superiores (puerto del tab bar de ClickGui.cpp) ---
    static int curTab = 0; // 0 = ClickGui, 1 = HudEditor
    struct TabDef { const char* name; };
    static const TabDef kTabs[] = {{"ClickGui"}, {"HudEditor"}};
    constexpr int kTabCount = 2;

    const float tabFont = 24.0f;
    const float pad = 20.0f;

    ImVec2 tabSizes[kTabCount];
    float totalW = 0.0f;
    for (int i = 0; i < kTabCount; ++i)
    {
        tabSizes[i] = font->CalcTextSizeA(tabFont, FLT_MAX, 0.0f, kTabs[i].name);
        totalW += tabSizes[i].x;
    }
    totalW += pad * (kTabCount - 1);

    float tabY = -35.0f + 45.0f * inScale;
    float x = (screen.x - totalW) * 0.5f;

    fg->AddRectFilled(ImVec2(x - pad, tabY), ImVec2(x + totalW, tabY + tabSizes[0].y + 5.0f),
                      IM_COL32(30, 30, 30, 200), 5.0f);

    ImVec2 tabPos[kTabCount];
    float tabBgH = tabSizes[0].y + 5.0f;
    for (int i = 0; i < kTabCount; ++i)
    {
        ImVec2 textPos(x, tabY + (tabBgH - tabSizes[i].y) * 0.5f);
        tabPos[i] = textPos;

        bool hover = mouse.x >= x && mouse.x <= x + tabSizes[i].x &&
                     mouse.y >= tabY && mouse.y <= tabY + tabBgH;
        if (hover && ImGui::IsMouseClicked(0) && !io.WantCaptureMouse)
            curTab = i;

        fg->AddText(font, tabFont, textPos,
                    i == curTab ? IM_COL32(255, 255, 255, 255) : IM_COL32(190, 193, 200, 220),
                    kTabs[i].name);
        x += tabSizes[i].x + pad;
    }

    static ImVec2 underlinePos(0.0f, 0.0f);
    static float underlineW = 0.0f;
    ImVec2 targetPos(tabPos[curTab].x - pad, tabPos[curTab].y + tabSizes[curTab].y + 2.0f);
    float lerp = dt * 10.0f;
    if (lerp > 1.0f)
        lerp = 1.0f;
    underlinePos.x += (targetPos.x - underlinePos.x) * lerp;
    underlinePos.y = targetPos.y;
    underlineW += (tabSizes[curTab].x - underlineW) * lerp;
    fg->AddLine(underlinePos, ImVec2(underlinePos.x + underlineW, underlinePos.y),
                IM_COL32(255, 255, 255, 255), 2.0f);

    // --- HudEditor: activa el editor real de Azyre mientras dura el tab ---
    menu::setHudEditing(curTab == 1);
    if (curTab == 1)
    {
        const char* msg = "HUD Editor: arrastra los widgets con el mouse";
        renderutils::drawText(ImVec2((screen.x - renderutils::getTextWidth(msg, 18.0f)) * 0.5f,
                                screen.y * 0.5f),
                         msg, IM_COL32(235, 238, 242, 255), 18.0f, fade, true, fg, font);
        return;
    }

    // --- Estilo solido para las ventanas ImGui de la GUI (search/settings) ---
    ImVec4 accentV4 = ImGui::ColorConvertU32ToFloat4(accent);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.12f, 0.97f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.94f, 0.96f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.60f, 0.62f, 0.66f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.13f, 0.13f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.16f, 0.16f, 0.19f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.18f, 0.18f, 0.21f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.13f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.17f, 0.17f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(accentV4.x * 0.6f, accentV4.y * 0.6f, accentV4.z * 0.6f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, accentV4);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, accentV4);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, accentV4);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.13f, 0.13f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                          ImVec4(accentV4.x, accentV4.y, accentV4.z, 0.18f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                          ImVec4(accentV4.x, accentV4.y, accentV4.z, 0.32f));
    ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.08f, 0.08f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.08f, 0.08f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.09f, 0.09f, 0.11f, 0.98f));

    // --- Search (solido, como ModernGui) ---
    static char search[128] = "";
    ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f - 150.0f, 100.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Search##sol", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::PushItemWidth(300.0f);
        ImGui::InputTextWithHint("##solsearch", "Search modules...", search, sizeof(search));
        ImGui::PopItemWidth();
    }
    ImGui::End();

    ModuleManager& mgr = ModuleManager::get();

    static CatState cat[6];
    const Category cats[] = {Category::Combat, Category::Movement,
                             Category::Visuals, Category::Misc,
                             Category::Scripting, Category::Interface};
    const int catCount = (int)(sizeof(cats) / sizeof(cats[0]));

    Module* hoveredMod = nullptr;
    Module* clickMod = nullptr;
    Module* expandMod = nullptr;

    struct SettingsWin
    {
        Module* m;
        ImVec4 slot;
    };
    std::vector<SettingsWin> settingsWins;

    auto& expanded = expandedMap();
    auto& expandAnim = expandAnimMap();
    auto& settingsH = settingsHeightMap();
    auto& enableAnim = enableAnimMap();

    // Lerp de animaciones de expand y de enable (cAnim/cScale de Solstice).
    for (auto& [m, open] : expanded)
    {
        float target = open ? 1.0f : 0.0f;
        float a = expandAnim[m];
        a += (target - a) * dt * 12.5f;
        expandAnim[m] = a;
    }
    for (auto& up : mgr.all())
    {
        Module* m = up.get();
        float target = m->enabled() ? 1.0f : 0.0f;
        float a = enableAnim[m];
        a += (target - a) * dt * 12.5f;
        enableAnim[m] = a;
    }

    // --- Paneles de categoria (custom-drawn, como ModernGui) ---
    {
        float total = catCount * kCatW + (catCount - 1) * kCatGap;
        float startX = (screen.x - total) * 0.5f;

        for (int i = 0; i < catCount; ++i)
        {
            std::vector<Module*> mods = mgr.byCategory(cats[i]);
            if (search[0] != '\0')
            {
                std::vector<Module*> filtered;
                for (Module* m : mods)
                    if (containsCI(m->name(), search))
                        filtered.push_back(m);
                mods = filtered;
            }
            if (mods.empty())
                continue;

            // Altura del contenido: filas + slots de settings animados.
            float contentH = 0.0f;
            for (Module* m : mods)
            {
                contentH += kRowH;
                float a = expandAnim.count(m) ? expandAnim[m] : 0.0f;
                if (a > 0.001f)
                    contentH += (settingsH.count(m) ? settingsH[m] : 140.0f) * a;
            }
            // El panel CRECE con los settings abiertos (catWindowHeight de
            // Solstice), limitado solo por la pantalla; el scroll cubre el
            // resto. Asi los settings nunca salen de su caja.
            float viewH = contentH;
            float maxViewH = screen.y - kTopY - kHeaderH - 70.0f;
            if (maxViewH < 120.0f)
                maxViewH = 120.0f;
            if (viewH > maxViewH)
                viewH = maxViewH;
            float panelH = cat[i].extended ? (kHeaderH + viewH) : kHeaderH;

            ImVec4 panel(startX + i * (kCatW + kCatGap), kTopY,
                         startX + i * (kCatW + kCatGap) + kCatW, kTopY + panelH);
            panel = scaleRect(panel, center, inScale);

            ImVec4 header(panel.x, panel.y, panel.z, panel.y + kHeaderH * inScale);

            // Scroll con lerp (animate * 10.5)
            float maxScroll = (contentH > viewH) ? (contentH - viewH) : 0.0f;
            if (cat[i].extended && renderutils::isMouseOver(panel) && !io.WantCaptureMouse)
            {
                if (io.MouseWheel > 0.0f)
                    cat[i].scrollT -= kRowH * 2.0f;
                else if (io.MouseWheel < 0.0f)
                    cat[i].scrollT += kRowH * 2.0f;
                if (cat[i].scrollT < 0.0f)
                    cat[i].scrollT = 0.0f;
                if (cat[i].scrollT > maxScroll)
                    cat[i].scrollT = maxScroll;
            }
            float sLerp = dt * 10.5f;
            if (sLerp > 1.0f)
                sLerp = 1.0f;
            if (!cat[i].extended)
                cat[i].scroll = cat[i].scrollT = 0.0f;
            else
                cat[i].scroll += (cat[i].scrollT - cat[i].scroll) * sLerp;

            // darkBlack (29,29,29) como el fillRectangle de ModernGui.
            renderutils::fillRectangle(panel, IM_COL32(29, 29, 29, 255), fade, 15.0f, 0, dl);
            renderutils::fillRectangle(header, IM_COL32(23, 23, 27, 255), fade, 15.0f,
                             ImDrawFlags_RoundCornersTop, dl);
            // Linea del color del tema bajo el header (el lineRect del original).
            renderutils::fillRectangle(ImVec4(header.x, header.w - 2.0f, header.z, header.w),
                              colors::getThemedColor(panel.y, 255), fade, 0.0f, 0, dl);

            // Nombre de la categoria centrado.
            {
                const char* name = categoryName(cats[i]);
                float fs = 18.0f;
                float tw = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, name).x;
                float th = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, "Ag").y;
                renderutils::drawText(ImVec2(header.x + (kCatW - tw) * 0.5f,
                                        header.y + (kHeaderH - th) * 0.5f),
                                 name, IM_COL32(255, 255, 255, 255), fs, fade, true, dl, font);
            }

            // Right-click en el header: colapsar/expandir (isExtended).
            if (renderutils::isMouseOver(header) && ImGui::IsMouseClicked(1) && !io.WantCaptureMouse)
                cat[i].extended = !cat[i].extended;

            if (!cat[i].extended)
                continue;

            // Filas + slots de settings, con cursor local (moduleY).
            dl->PushClipRect(ImVec2(panel.x, header.w), ImVec2(panel.z, panel.w), true);

            float moduleY = -cat[i].scroll;
            int rowIdx = 0;
            for (Module* m : mods)
            {
                float rowH = kRowH;
                float slotH = 0.0f;
                bool open = expanded.count(m) && expanded[m];
                {
                    // La altura del slot sigue la animacion tanto al abrir
                    // como al cerrar (cAnim de Solstice).
                    float a = expandAnim.count(m) ? expandAnim[m] : 0.0f;
                    if (a > 0.001f)
                    {
                        float baseH = settingsH.count(m) ? settingsH[m] : 140.0f;
                        slotH = baseH * a;
                    }
                }

                if (moduleY + rowH + slotH > 0.0f && moduleY < viewH)
                {
                    ImVec4 row(panel.x, header.w + moduleY, panel.z,
                               header.w + moduleY + rowH);
                    row = scaleRect(row, center, inScale);

                    bool on = m->enabled();
                    float cScale = enableAnim.count(m) ? enableAnim[m] : (on ? 1.0f : 0.0f);

                    // Fondo: gradiente del tema cuando ON (rgb1/rgb2 con los
                    // indices del original), panel oscuro cuando OFF. La
                    // ultima fila cierra el panel: esquinas inferiores
                    // redondeadas como el original (radius 15*(1-cAnim)).
                    if (cScale > 0.001f)
                    {
                        ImU32 rgb1 = colors::getThemedColor(moduleY * 2.0f, 255.0f);
                        ImU32 rgb2 = colors::getThemedColor(moduleY * 2.0f + kCatW * 1.4f, 255.0f);
                        float gRad = 0.0f;
                        ImDrawFlags gFlags = 0;
                        if (rowIdx == (int)mods.size() - 1)
                        {
                            gFlags = ImDrawFlags_RoundCornersBottom;
                            float ea = expandAnim.count(m) ? expandAnim[m] : 0.0f;
                            gRad = 15.0f * (1.0f - ea);
                        }
                        renderutils::fillRoundedGradientRectangle(row, rgb1, rgb2, gRad,
                                                                 fade * cScale, fade * cScale,
                                                                 gFlags, dl);
                    }

                    // Hover: overlay + acciones + tooltip.
                    bool rowHover = renderutils::isMouseOver(row) && !io.WantCaptureMouse;
                    if (rowHover)
                    {
                        renderutils::fillRectangle(row, IM_COL32(255, 255, 255, 255),
                                                   0.06f * fade, 0.0f, 0, dl);
                        hoveredMod = m;
                        // Click izquierdo: toggle. Derecho: expandir la
                        // configuracion del modulo (como Solstice).
                        if (ImGui::IsMouseClicked(0))
                            clickMod = m;
                        if (ImGui::IsMouseClicked(1))
                            expandMod = m;
                    }

                    // Nombre centrado (blanco ON / 180,180,180 OFF).
                    {
                        float fs = 16.0f;
                        float tw = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, m->name().c_str()).x;
                        float th = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, "Ag").y;
                        renderutils::drawText(
                            ImVec2(row.x + (kCatW - tw) * 0.5f,
                                   row.y + (kRowH - th) * 0.5f),
                            m->name().c_str(),
                            on ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 180, 180, 255),
                            fs, fade, true, dl, font);
                    }
                }

                moduleY += rowH;

                // Slot de settings: corre las filas de abajo hacia abajo
                // (settingsHeight + cAnim de Solstice).
                if (slotH > 0.5f)
                {
                    if (moduleY + slotH > 0.0f && moduleY < viewH)
                    {
                        ImVec4 slot(panel.x, header.w + moduleY, panel.z,
                                    header.w + moduleY + slotH);
                        ImVec4 scaledSlot = scaleRect(slot, center, inScale);
                        // El slot nunca sale del panel: la ventana de settings
                        // se recorta al fondo de la categoria.
                        if (scaledSlot.w > panel.w)
                            scaledSlot.w = panel.w;
                        renderutils::fillRectangle(scaledSlot, IM_COL32(22, 22, 26, 255), fade, 0.0f, 0, dl);
                        renderutils::fillRectangle(ImVec4(scaledSlot.x, scaledSlot.y, scaledSlot.x + 2.5f,
                                                scaledSlot.w),
                                         colors::getThemedColor(moduleY * 2.0f, 255), fade * 0.9f, 0.0f, 0, dl);
                        settingsWins.push_back({m, scaledSlot});
                    }
                    moduleY += slotH;
                }

                ++rowIdx;
            }

            dl->PopClipRect();
        }
    }

    // Settings: ventana clavada EXACTAMENTE al slot del panel. Padding 0 y
    // espaciado 0 para que las filas queden flush como los modHeight de
    // Solstice; la altura natural se mide en vivo con el cursor del
    // contenido (el slot crece con la anim y el contenido se revela
    // recortado por la propia ventana).
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    for (auto& swin : settingsWins)
    {
        if (!expanded[swin.m])
            continue;
        float slotW = swin.slot.z - swin.slot.x;
        float slotH = swin.slot.w - swin.slot.y;
        if (slotW < 20.0f || slotH < 1.0f)
            continue;

        // Nombre unico por modulo: si todos comparten "##solsettings", ImGui
        // los colapsa en una sola ventana (los settings del primer modulo
        // desaparecen hasta colapsar el segundo).
        char winTitle[96];
        snprintf(winTitle, sizeof(winTitle), "##solsettings_%s", swin.m->name().c_str());
        ImGui::SetNextWindowPos(ImVec2(swin.slot.x, swin.slot.y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(slotW, slotH), ImGuiCond_Always);
        if (ImGui::Begin(winTitle, nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground))
        {
            swin.m->drawSettings();
            // Altura natural del contenido (coords relativas, padding 0):
            // alimenta el slot del frame siguiente sin desfase.
            float contentEnd = ImGui::GetCursorPosY();
            if (contentEnd > 20.0f)
                settingsH[swin.m] = contentEnd;
        }
        ImGui::End();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(19);

    // Expand/collapse del chevron (despues de renderizar: el toggle entra
    // al frame siguiente con la animacion).
    if (expandMod)
        expanded[expandMod] = !expanded[expandMod];

    // Acciones de fila: click = toggle, right-click = expandir settings.
    if (clickMod)
        clickMod->toggle();

    // Tooltip con la descripcion del modulo bajo el cursor.
    if (hoveredMod && !hoveredMod->description().empty() && !mgr.binding())
    {
        const char* desc = hoveredMod->description().c_str();
        float fs = 14.0f;
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, desc);
        ImVec2 tp(mouse.x + 14.0f, mouse.y + 16.0f);
        if (tp.x + ts.x + 12.0f > screen.x)
            tp.x = mouse.x - ts.x - 14.0f;
        fg->AddRectFilled(ImVec2(tp.x - 6.0f, tp.y - 4.0f),
                          ImVec2(tp.x + ts.x + 6.0f, tp.y + ts.y + 4.0f),
                          IM_COL32(20, 20, 24, 240), 6.0f);
        fg->AddText(font, fs, tp, IM_COL32(220, 223, 228, 240), desc);
    }

    // Hint de binding mientras se captura una tecla.
    if (mgr.binding())
    {
        const char* msg = "Binding: presiona una tecla  |  ESC limpia";
        float fs = 16.0f;
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, msg);
        fg->AddText(font, fs, ImVec2((screen.x - ts.x) * 0.5f, screen.y - 40.0f),
                    IM_COL32(255, 210, 120, (int)(255.0f * fade)), msg);
    }
}

}
