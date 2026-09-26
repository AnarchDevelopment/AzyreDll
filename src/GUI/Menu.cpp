#include "Menu.hpp"

#include "Config/Config.hpp"
#include "Features/PlaceStats.hpp"
#include "Framework/Log.hpp"
#include "Framework/Math.hpp"
#include "GUI/Theme.hpp"
#include "GUI/Widgets.hpp"
#include "Input/WheelHook.hpp"
#include "Modules/ModuleManager.hpp"
#include "Render/DX11.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <windows.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace mc::menu {

static bool g_visible = false;
static int g_tab = 0;
static float g_anim = 0.0f;
static ImVec2 g_menuMin = ImVec2(0, 0);
static ImVec2 g_menuMax = ImVec2(0, 0);

static bool g_debugOpen = false;
static char g_search[64] = "";

static std::map<const Module*, bool> g_expanded;
static std::map<const Module*, float> g_expandAnim;
static std::map<const Module*, float> g_switchAnim;
static std::map<const Module*, float> g_rowHoverAnim;

bool visible()
{
    return g_visible;
}

float animationAlpha()
{
    return g_anim;
}

void handleInput()
{
    static bool wasDown = false;
    bool isDown = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    if (isDown && !wasDown)
    {
        g_visible = !g_visible;
        if (!g_visible)
            config::saveAll();
    }
    wasDown = isDown;
}

static const char* keyName(int key)
{
    static char buf[16];
    if (key <= 0)
        return "NONE";
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
    {
        buf[0] = (char)key;
        buf[1] = 0;
        return buf;
    }
    snprintf(buf, sizeof(buf), "VK_%02X", key);
    return buf;
}

static bool containsCI(const std::string& hay, const char* needle)
{
    size_t nl = strlen(needle);
    if (nl == 0)
        return true;
    if (hay.size() < nl)
        return false;
    std::string h;
    h.reserve(hay.size());
    for (char c : hay)
        h.push_back((char)std::tolower((unsigned char)c));
    std::string n;
    n.reserve(nl);
    for (size_t i = 0; i < nl; ++i)
        n.push_back((char)std::tolower((unsigned char)needle[i]));
    return h.find(n) != std::string::npos;
}

// Sombra suave y panel acrilico viven en widgets:: (SoftShadow/AcrylicPanel).

static void drawStatusPill(ImDrawList* d, const ImVec2& linePos, float winRight)
{
    bool inWorld = Game::get().localFound();
    const char* st = inWorld ? "IN WORLD" : "MENU";
    ImVec2 ts = ImGui::CalcTextSize(st);
    float pillRight = winRight - 40.0f;
    ImVec2 pMin(pillRight - ts.x - 30.0f, linePos.y - 4.0f);
    ImVec2 pMax(pillRight, linePos.y + ts.y + 4.0f);
    float h = pMax.y - pMin.y;

    ImU32 bg = inWorld ? IM_COL32(107, 203, 98, 26) : IM_COL32(255, 255, 255, 16);
    ImU32 dot = inWorld ? IM_COL32(107, 203, 98, 235) : IM_COL32(150, 155, 162, 235);
    ImU32 txt = inWorld ? IM_COL32(138, 226, 132, 255) : IM_COL32(190, 193, 198, 255);

    d->AddRectFilled(pMin, pMax, bg, h * 0.5f);
    d->AddCircleFilled(ImVec2(pMin.x + 9.0f, (pMin.y + pMax.y) * 0.5f), 3.2f, dot, 10);
    d->AddText(ImVec2(pMin.x + 18.0f, pMin.y + 4.0f), txt, st);
}

void render()
{
    ModuleManager& mgr = ModuleManager::get();

    theme::apply();

    float dt = ImGui::GetIO().DeltaTime;
    float speed = dt * 9.0f;
    if (speed > 1.0f)
        speed = 1.0f;
    float target = g_visible ? 1.0f : 0.0f;
    g_anim += (target - g_anim) * speed;
    if (g_visible && g_anim > 0.995f)
        g_anim = 1.0f;
    if (!g_visible && g_anim < 0.005f)
        g_anim = 0.0f;

    if (!g_visible && g_anim <= 0.0f)
        return;

    float t = g_anim;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    float ease = (t >= 0.9999f) ? 1.0f : (1.0f - std::pow(2.0f, -10.0f * t));
    float scale = 0.15f + 0.85f * ease;

    ImVec2 disp = ImGui::GetIO().DisplaySize;

    if (ease > 0.01f)
    {
        ImGui::GetBackgroundDrawList()->AddRectFilled(
            ImVec2(0.0f, 0.0f), disp, IM_COL32(6, 8, 10, (int)(175.0f * ease)));
    }

    ImGui::SetNextWindowPos(ImVec2(disp.x * 0.5f, disp.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(760.0f * scale, 540.0f * scale), ImGuiCond_Always);

    float bgAlpha = ease * 1.4f;
    if (bgAlpha > 1.0f)
        bgAlpha = 1.0f;
    ImGui::SetNextWindowBgAlpha(0.97f * bgAlpha);

    float winAlpha = 0.10f + 0.90f * ease;
    if (winAlpha > 1.0f)
        winAlpha = 1.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, winAlpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * scale, 10.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(9.0f * scale, 5.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(9.0f * scale, 6.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);

    bool open = g_visible;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoTitleBar;
    bool isWindowOpen = ImGui::Begin("Azyre", g_visible ? &open : nullptr, flags);
    if (isWindowOpen)
    {
        theme::apply();
        ImVec2 winPos = ImGui::GetWindowPos();
        float winW = ImGui::GetWindowWidth();
        g_menuMin = winPos;
        g_menuMax = ImVec2(winPos.x + winW, winPos.y + ImGui::GetWindowSize().y);

        // Gradiente tipo Mica: leve brillo superior que se desvanece.
        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
            winPos, ImVec2(g_menuMax.x, winPos.y + 90.0f),
            IM_COL32(255, 255, 255, 11), IM_COL32(255, 255, 255, 11),
            IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));

        // Sombra elevacion + backdrop acrilico (blur del frame) bajo la ventana.
        {
            ImDrawList* bg = ImGui::GetBackgroundDrawList();
            ImVec2 bMin(winPos.x, winPos.y);
            ImVec2 bMax(g_menuMax.x, g_menuMax.y);
            widgets::SoftShadow(bg, bMin, bMax, 8.0f, 46.0f * ease, 22.0f);
            if (ease > 0.01f && dx11::backdropWanted() && dx11::backdropReady())
            {
                bg->AddImageRounded(ImTextureRef((void*)dx11::backdropSrv()),
                                    bMin, bMax,
                                    ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                                    IM_COL32(255, 255, 255, (int)(238.0f * ease)), 8.0f);
            }
        }

        // ================= Header =================
        ImGui::PushStyleColor(ImGuiCol_Text, theme::Accent);
        ImGui::Text("AZYRE");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("1.1.0  Bedrock Win10 DX11");

        drawStatusPill(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), winPos.x + winW);

        ImGui::SameLine(winW - 36.0f);
        if (ImGui::SmallButton("X##close_menu"))
            open = false;

        ImGui::PushStyleColor(ImGuiCol_Separator,
                              ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z, 0.40f));
        ImGui::Separator();
        ImGui::PopStyleColor();

        // ================= Layout =================
        static int lastTabShown = -1;
        static float tabAnim = 1.0f;
        if (g_tab != lastTabShown)
        {
            lastTabShown = g_tab;
            tabAnim = 0.0f;
        }
        tabAnim = clampf(tabAnim + dt * 6.0f, 0.0f, 1.0f);
        float tabEase = widgets::EaseOutExpo(tabAnim);

        const float sideW = 152.0f;
        const float tabH = 30.0f;
        const float tabGap = 5.0f;

        ImVec2 areaBase = ImGui::GetCursorPos();
        float totalW = ImGui::GetContentRegionAvail().x;
        float contentW = totalW - sideW - 6.0f;

        bool searching = (g_tab != 4) && g_search[0] != '\0';
        float searchRowH = (g_tab != 4) ? 32.0f : 0.0f;

        float debugH = g_debugOpen ? 92.0f : 0.0f;
        float footerH = 74.0f + debugH;
        float availH = ImGui::GetContentRegionAvail().y;
        float colsH = availH - footerH - searchRowH;
        if (colsH < 150.0f)
            colsH = 150.0f;

        static float indY = -1.0f;
        if (indY < 0.0f)
            indY = areaBase.y;
        float targetY = areaBase.y + g_tab * (tabH + tabGap);
        indY += (targetY - indY) * clampf(dt * 14.0f, 0.0f, 1.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, winAlpha * (0.40f + 0.60f * tabEase));

        // ================= Search bar =================
        if (g_tab != 4)
        {
            ImGui::SetNextItemWidth(totalW * 0.45f);
            ImGui::InputTextWithHint("##search", "Search module...", g_search, sizeof(g_search));
            if (g_search[0] != '\0')
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("x##clearsearch"))
                    g_search[0] = '\0';
            }
        }

        ImGui::SetCursorPos(ImVec2(areaBase.x, areaBase.y + searchRowH));

        // ================= Sidebar (tabs, izquierda) =================
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 8.0f));
        if (ImGui::BeginChild("##side", ImVec2(sideW, colsH), false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            const char* tabs[] = {"Combat", "Movement", "Visuals", "Misc", "Configs"};
            ImGuiWindow* sideWnd = ImGui::GetCurrentWindow();
            float bbW = ImGui::GetContentRegionAvail().x;

            for (int i = 0; i < 5; ++i)
            {
                ImGui::SetCursorPos(ImVec2(0.0f, i * (tabH + tabGap)));
                if (sideWnd->SkipItems)
                    continue;

                ImVec2 bbMin = ImGui::GetCursorScreenPos();
                ImRect bb(bbMin, ImVec2(bbMin.x + bbW, bbMin.y + tabH));
                ImGui::ItemSize(ImVec2(bbW, tabH), 0.0f);

                bool hovered = ImGui::IsMouseHoveringRect(bb.Min, bb.Max) &&
                               ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                bool pressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
                if (pressed)
                {
                    g_tab = i;
                    g_search[0] = '\0';
                }

                bool act = g_tab == i && !searching;
                ImDrawList* d = sideWnd->DrawList;

                // Fade de hover estilo Fluent.
                static float tabHoverA[5] = {};
                tabHoverA[i] += (((hovered && !act) ? 1.0f : 0.0f) - tabHoverA[i]) *
                                clampf(dt * 12.0f, 0.0f, 1.0f);

                if (act)
                    d->AddRectFilled(bb.Min, bb.Max, theme::AccentU32(0.14f), 4.0f);
                else if (tabHoverA[i] > 0.01f)
                    d->AddRectFilled(bb.Min, bb.Max,
                                    IM_COL32(255, 255, 255, (int)(20.0f * tabHoverA[i])), 4.0f);

                const char* text = tabs[i];
                ImVec2 ts = ImGui::CalcTextSize(text);
                ImVec2 textPos = ImVec2(bb.Min.x + 12.0f,
                                        bb.Min.y + (tabH - ts.y) * 0.5f);
                ImU32 textCol = act ? theme::AccentU32()
                                    : ImGui::GetColorU32(ImGuiCol_Text);
                d->AddText(textPos, textCol, text);

                if (i < 4)
                {
                    int n = (int)mgr.byCategory((Category)i).size();
                    if (n > 0)
                    {
                        char cnt[16];
                        snprintf(cnt, sizeof(cnt), "%d", n);
                        ImVec2 cs = ImGui::CalcTextSize(cnt);
                        d->AddText(ImVec2(bb.Max.x - cs.x - 10.0f,
                                          bb.Min.y + (tabH - cs.y) * 0.5f),
                                   act ? theme::AccentU32(0.75f) : IM_COL32(115, 122, 133, 220),
                                   cnt);
                    }
                }
            }

            ImVec2 sPos = ImGui::GetWindowPos();
            float localInd = indY - areaBase.y;
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(sPos.x + 2.0f, sPos.y + localInd + 5.0f),
                ImVec2(sPos.x + 5.0f, sPos.y + localInd + tabH - 5.0f),
                theme::AccentU32(), 1.5f);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::SameLine(0.0f, 6.0f);

        // ================= Contenido =================
        if (g_tab == 4)
        {
            if (ImGui::BeginChild("##configs", ImVec2(contentW, colsH), false))
            {
                static char newName[64] = "";
                static std::string selectedName;
                static std::vector<std::string> presets;
                static ULONGLONG lastRefresh = 0;
                static ULONGLONG lastToast = 0;
                static char toast[128] = "";

                auto refresh = [&]()
                {
                    presets = config::listPresets();
                    lastRefresh = GetTickCount64();
                    if (!selectedName.empty() &&
                        std::find(presets.begin(), presets.end(), selectedName) == presets.end())
                        selectedName.clear();
                };

                if (presets.empty() || GetTickCount64() - lastRefresh > 1000)
                    refresh();

                float cfgW = ImGui::GetContentRegionAvail().x;

                // ---- Crear ----
                ImGui::TextDisabled("New config from current state:");
                ImGui::SetNextItemWidth(cfgW - 130.0f);
                ImGui::InputTextWithHint("##cfgname", "Name...", newName, sizeof(newName));
                ImGui::SameLine();
                if (ImGui::Button("Create##cfg_create") && newName[0] != '\0')
                {
                    if (config::savePreset(newName))
                    {
                        snprintf(toast, sizeof(toast), "Config created: %s", newName);
                        lastToast = GetTickCount64();
                        selectedName = newName;
                        newName[0] = '\0';
                        refresh();
                    }
                }

                ImGui::Separator();

                // ---- Listado ----
                float listH = ImGui::GetContentRegionAvail().y - 62.0f;
                if (ImGui::BeginChild("##cfglist", ImVec2(cfgW, listH), true))
                {
                    if (presets.empty())
                        ImGui::TextDisabled("No configs saved yet");

                    int idx = 0;
                    for (const std::string& p : presets)
                    {
                        ImGui::PushID(idx++);
                        bool isSel = (p == selectedName);
                        if (isSel)
                            ImGui::PushStyleColor(ImGuiCol_Text, theme::Accent);
                        if (ImGui::Selectable(p.c_str(), isSel))
                            selectedName = p;
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            config::loadPreset(p);
                            snprintf(toast, sizeof(toast), "Config loaded: %s", p.c_str());
                            lastToast = GetTickCount64();
                        }
                        if (isSel)
                            ImGui::PopStyleColor();
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();

                // ---- Acciones sobre la seleccion ----
                bool hasSel = !selectedName.empty();
                if (!hasSel)
                    ImGui::BeginDisabled();

                if (ImGui::Button("Load##cfg_load"))
                {
                    if (config::loadPreset(selectedName))
                    {
                        snprintf(toast, sizeof(toast), "Config loaded: %s", selectedName.c_str());
                        lastToast = GetTickCount64();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Save##cfg_save"))
                {
                    if (config::savePreset(selectedName))
                    {
                        snprintf(toast, sizeof(toast), "Config saved: %s", selectedName.c_str());
                        lastToast = GetTickCount64();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete##cfg_del"))
                {
                    if (config::deletePreset(selectedName))
                    {
                        snprintf(toast, sizeof(toast), "Config deleted: %s", selectedName.c_str());
                        lastToast = GetTickCount64();
                        selectedName.clear();
                        refresh();
                    }
                }

                if (!hasSel)
                    ImGui::EndDisabled();

                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 210.0f);
                if (toast[0] && GetTickCount64() - lastToast < 2500)
                    ImGui::TextDisabled("%s", toast);
                else if (!hasSel)
                    ImGui::TextDisabled("Select a config");
                else
                    ImGui::TextDisabled("Selected: %s", selectedName.c_str());
            }
            ImGui::EndChild();
        }
        else
        {
            if (ImGui::BeginChild("##modlist", ImVec2(contentW, colsH), false))
            {
                float wheelY = ImGui::GetIO().MouseWheel;
                ImVec2 mousePos = ImGui::GetIO().MousePos;
                bool mouseInMenu = mousePos.x >= g_menuMin.x && mousePos.x <= g_menuMax.x &&
                                   mousePos.y >= g_menuMin.y && mousePos.y <= g_menuMax.y;
                if (wheelY != 0.0f && mouseInMenu && ImGui::GetScrollMaxY() > 0.0f &&
                    GImGui->WheelingWindowScrolledFrame != ImGui::GetFrameCount())
                {
                    float step = ImTrunc(ImMin(5.0f * ImGui::GetFontSize(),
                                               ImGui::GetWindowHeight() * 0.67f));
                    ImGui::SetScrollY(ImGui::GetScrollY() - wheelY * step);
                }

                ImDrawList* dl = ImGui::GetWindowDrawList();
                Module* binding = mgr.binding();
                std::vector<Module*> modules;
                if (searching)
                {
                    for (auto& up : mgr.all())
                    {
                        Module* m = up.get();
                        if (m && containsCI(m->name(), g_search))
                            modules.push_back(m);
                    }
                }
                else
                {
                    modules = mgr.byCategory((Category)g_tab);
                }

                if (searching)
                {
                    char lbl[48];
                    snprintf(lbl, sizeof(lbl), "Results (%d)", (int)modules.size());
                    ImGui::TextDisabled("%s", lbl);
                }

                const float rowH = 34.0f;
                const float chipZone = 168.0f;

                for (Module* m : modules)
                {
                    if (!m)
                        continue;

                    float avail = ImGui::GetContentRegionAvail().x;
                    ImVec2 rowMin = ImGui::GetCursorPos();
                    // Posicion en pantalla YA con el scroll del child aplicado:
                    // GetCursorPos() es relativo al contenido y no incluye el desplazamiento.
                    ImVec2 sMin = ImGui::GetCursorScreenPos();
                    ImVec2 sMax(sMin.x + avail, sMin.y + rowH);
                    ImGui::PushID(m);

                    // Fila clicable: activar/desactivar
                    ImGui::SetCursorPos(rowMin);
                    if (ImGui::InvisibleButton("##row", ImVec2(avail - chipZone, rowH)))
                    {
                        bool turningOn = !m->enabled();
                        m->setEnabled(turningOn);
                        if (turningOn && m->hasSettings())
                            g_expanded[m] = true;
                    }
                    bool rowHov = ImGui::IsItemHovered();
                    if (rowHov && !m->description().empty())
                        ImGui::SetTooltip("%s", m->description().c_str());

                    // Hover animado
                    float ha = g_rowHoverAnim[m];
                    ha += ((rowHov ? 1.0f : 0.0f) - ha) * clampf(dt * 14.0f, 0.0f, 1.0f);
                    g_rowHoverAnim[m] = ha;
                    if (ha > 0.01f)
                        dl->AddRectFilled(sMin, sMax, IM_COL32(255, 255, 255, (int)(13.0f * ha)), 3.0f);

                    bool on = m->enabled();

                    // Barra de acento a la izquierda cuando esta activo
                    if (on)
                        dl->AddRectFilled(sMin, ImVec2(sMin.x + 3.0f, sMax.y),
                                          theme::AccentU32(0.85f), 1.5f);

                    // Toggle estilo WinUI dark: track azul claro con perilla negra.
                    float& sw = g_switchAnim[m];
                    sw += ((on ? 1.0f : 0.0f) - sw) * clampf(dt * 14.0f, 0.0f, 1.0f);
                    float swW = 34.0f, swH = 18.0f;
                    ImVec2 swMin(sMin.x + 9.0f, sMin.y + (rowH - swH) * 0.5f);
                    ImVec2 swMax(swMin.x + swW, swMin.y + swH);
                    dl->AddRectFilled(swMin, swMax,
                                      on ? theme::AccentU32(0.95f) : IM_COL32(255, 255, 255, 20),
                                      swH * 0.5f);
                    if (!on)
                        dl->AddRect(swMin, swMax, IM_COL32(255, 255, 255, 26), swH * 0.5f, 0, 1.0f);
                    float kx = swMin.x + 7.0f + sw * (swW - 14.0f);
                    dl->AddCircleFilled(ImVec2(kx, (swMin.y + swMax.y) * 0.5f), 5.5f,
                                        on ? IM_COL32(17, 18, 21, 255) : IM_COL32(235, 238, 242, 255), 12);

                    // Nombre
                    const char* nm = m->name().c_str();
                    ImVec2 nts = ImGui::CalcTextSize(nm);
                    float nameX = sMin.x + 9.0f + swW + 10.0f;
                    dl->AddText(ImVec2(nameX, sMin.y + (rowH - nts.y) * 0.5f),
                                on ? theme::AccentU32() : ImGui::GetColorU32(ImGuiCol_Text), nm);

                    if (searching)
                    {
                        const char* cat = categoryName(m->category());
                        ImVec2 cts = ImGui::CalcTextSize(cat);
                        dl->AddText(ImVec2(nameX + nts.x + 8.0f, sMin.y + (rowH - cts.y) * 0.5f),
                                    IM_COL32(140, 148, 160, 220), cat);
                    }

                    // Chip de keybind
                    char label[64];
                    if (binding == m)
                        snprintf(label, sizeof(label), "[...]##bind");
                    else
                        snprintf(label, sizeof(label), "[%s]##bind", keyName(m->keybind()));
                    ImGui::SetCursorPos(ImVec2(avail - chipZone + 4.0f, rowMin.y + 6.0f));
                    if (ImGui::SmallButton(label))
                    {
                        if (mgr.binding() == m)
                            mgr.setBinding(nullptr);
                        else
                            mgr.setBinding(m);
                    }

                    // Flecha de settings
                    if (m->hasSettings())
                    {
                        bool exp = g_expanded[m];
                        float& ea = g_expandAnim[m];
                        ea += ((exp ? 1.0f : 0.0f) - ea) * clampf(dt * 12.0f, 0.0f, 1.0f);

                        ImGui::SetCursorPos(ImVec2(avail - 36.0f, rowMin.y + 7.0f));
                        if (ImGui::InvisibleButton("##exp", ImVec2(20.0f, 20.0f)))
                            g_expanded[m] = !exp;

                        ImVec2 bMin = ImGui::GetItemRectMin();
                        ImVec2 ctr(bMin.x + 10.0f, bMin.y + 10.0f);
                        float ang = ea * 1.5707963f;
                        float ca = cosf(ang), sa = sinf(ang);
                        auto rot = [&](float x, float y) {
                            return ImVec2(ctr.x + x * ca - y * sa, ctr.y + x * sa + y * ca);
                        };
                        ImU32 arCol = (ea > 0.01f)
                                          ? theme::AccentU32()
                                          : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                        dl->AddTriangleFilled(rot(-3.0f, -4.5f), rot(-3.0f, 4.5f),
                                              rot(4.5f, 0.0f), arCol);
                    }

                    // Panel de settings
                    if (m->hasSettings() && g_expanded[m])
                    {
                        ImGui::SetCursorPos(ImVec2(0.0f, rowMin.y + rowH + 2.0f));
                        ImVec2 setTopScreen = ImGui::GetCursorScreenPos();
                        ImGui::Indent(52.0f);
                        m->drawSettings();
                        ImGui::Unindent(52.0f);
                        float setBotY = ImGui::GetCursorPos().y;
                        ImVec2 setBotScreen = ImGui::GetCursorScreenPos();

                        dl->AddRectFilled(ImVec2(setTopScreen.x + 40.0f, setTopScreen.y + 2.0f),
                                          ImVec2(setTopScreen.x + 42.5f, setBotScreen.y - 2.0f),
                                          theme::AccentU32(0.30f), 1.0f);

                        ImGui::SetCursorPos(ImVec2(0.0f, setBotY + 8.0f));
                    }
                    else
                    {
                        ImGui::SetCursorPos(ImVec2(0.0f, rowMin.y + rowH + 4.0f));
                    }

                    ImGui::PopID();
                }

                // ImGui 1.92: neutraliza el SetCursorPos final de la ultima fila
                // (exigir un item despues evita el assert de boundaries en End()).
                ImGui::Dummy(ImVec2(0.0f, 0.0f));

                if (modules.empty() && !searching)
                    ImGui::TextDisabled("No modules in this category");
            }
            ImGui::EndChild();
        }

        ImGui::PopStyleVar();

        // Divisor vertical entre sidebar y contenido
        ImVec2 dividerTop(winPos.x + areaBase.x + sideW + 3.0f,
                           winPos.y + areaBase.y + searchRowH - 2.0f);
        ImVec2 dividerBot(winPos.x + areaBase.x + sideW + 3.0f,
                          winPos.y + areaBase.y + searchRowH + colsH + 4.0f);
        ImGui::GetWindowDrawList()->AddLine(dividerTop, dividerBot, theme::AccentU32(0.30f), 1.0f);

        // ================= Footer =================
        ImGui::SetCursorPos(ImVec2(areaBase.x, areaBase.y + searchRowH + colsH + 10.0f));
        ImGui::Separator();
        {
            Game& g = Game::get();
            bool world = g.localFound();
            bool gmOk = false;
            bool inputOk = false;
            if (world)
            {
                gmOk = g.gameMode().valid();
                inputOk = g.moveInput().valid();
            }

            ImGui::TextDisabled("LP: %s   Remotes: %d/%d   GameMode: %s   Input: %s",
                                world ? "OK" : "--",
                                (int)g.shownRemotesCount(), (int)g.rawRemotesCount(),
                                gmOk ? "OK" : "FAIL", inputOk ? "OK" : "FAIL");

            ImGui::SameLine(winW - 76.0f);
            if (ImGui::SmallButton(g_debugOpen ? "Debug -##foot" : "Debug +##foot"))
                g_debugOpen = !g_debugOpen;

            if (g_debugOpen)
            {
                if (world)
                {
                    ItemInstance held = g.localPlayer().heldItem();
                    ImGui::TextDisabled("Held: %s  count=%d  block=%s",
                                        held.valid() ? "yes" : "NO",
                                        held.valid() ? held.count() : 0,
                                        held.isBlockItem() ? "yes" : "no");

                    LocalPlayer lp = g.localPlayer();
                    ImGui::TextDisabled("Ground: %d  LMB: %d  vy: %.2f  airH: %.2f",
                                        lp.onGround() ? 1 : 0,
                                        lp.leftClickFlag() ? 1 : 0,
                                        lp.velocity().y,
                                        lp.height());
                }

                ImGui::TextDisabled("Place: %llu tries  last=%s  @ %d,%d,%d   Wheel: %.2f",
                                    placestats::attempts,
                                    placestats::lastOk ? "OK" : "fail",
                                    placestats::lastX, placestats::lastY, placestats::lastZ,
                                    wheel::lastValue());
            }
        }
        ImGui::Separator();

        if (mgr.binding())
            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f),
                               "Binding: press a key | ESC clears | click [...] to cancel");
        else
            ImGui::TextDisabled("INSERT: show/hide   ·   click row: toggle   ·   [key]: bind   ·   arrow: settings");
    }

    ImGui::End();
    ImGui::PopStyleVar(5);

    if (!open)
    {
        g_visible = false;
        config::saveAll();
    }
}

void renderToasts()
{
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 0.0f || sh <= 0.0f)
        return;

    const auto& notifs = ModuleManager::get().notifications();
    ULONGLONG now = GetTickCount64();
    ImDrawList* d = ImGui::GetForegroundDrawList();
    float y = sh - 44.0f;

    for (auto it = notifs.rbegin(); it != notifs.rend(); ++it)
    {
        ULONGLONG age = now - it->time;
        if (age > 2500)
            continue;

        float a = 1.0f;
        if (age < 150)
            a = (float)age / 150.0f;
        else if (age > 2000)
            a = 1.0f - (float)(age - 2000) / 500.0f;
        if (a < 0.0f)
            a = 0.0f;

        char buf[128];
        if (it->hasState)
            snprintf(buf, sizeof(buf), "%s  [%s]", it->text.c_str(), it->enabled ? "ON" : "OFF");
        else
            snprintf(buf, sizeof(buf), "%s", it->text.c_str());

        ImVec2 ts = ImGui::CalcTextSize(buf);
        float w = ts.x + 24.0f;
        float h = ts.y + 12.0f;
        float slide = (1.0f - a) * 30.0f;
        float x0 = sw - 14.0f - w + slide;

        ImVec2 tMin(x0, y - h);
        ImVec2 tMax(x0 + w, y);
        bool haveBlur = dx11::backdropWanted() && dx11::backdropReady();

        // Sombra suave + acrilico local (sub-rect del blur global).
        widgets::SoftShadow(d, tMin, tMax, 8.0f, 34.0f * a, 14.0f);
        if (haveBlur)
        {
            ImVec2 uv0(tMin.x / sw, tMin.y / sh);
            ImVec2 uv1(tMax.x / sw, tMax.y / sh);
            d->AddImageRounded(ImTextureRef((void*)dx11::backdropSrv()),
                               tMin, tMax, uv0, uv1,
                               IM_COL32(255, 255, 255, (int)(255.0f * a)), 8.0f);
        }

        int cardA = haveBlur ? 80 : 235;
        d->AddRectFilled(tMin, tMax, IM_COL32(16, 20, 28, (int)(cardA * a)), 8.0f);
        d->AddRect(tMin, tMax, IM_COL32(255, 255, 255, (int)(18 * a)), 8.0f, 0, 1.0f);
        d->AddRectFilled(ImVec2(x0, y - h), ImVec2(x0 + 3.5f, y),
                         it->enabled ? theme::AccentU32(a)
                                     : IM_COL32(150, 155, 160, (int)(255 * a)),
                         8.0f);
        d->AddText(ImVec2(x0 + 12.0f, y - h + 6.0f),
                   IM_COL32(240, 242, 245, (int)(255 * a)), buf);
        y -= h + 6.0f;
    }
}

}
