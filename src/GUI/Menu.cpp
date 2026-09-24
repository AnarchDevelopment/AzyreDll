#include "Menu.hpp"

#include "Features/PlaceStats.hpp"
#include "Framework/Math.hpp"
#include "GUI/Theme.hpp"
#include "GUI/Widgets.hpp"
#include "Input/WheelHook.hpp"
#include "Modules/ModuleManager.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

namespace mc::menu {

static bool g_visible = false;
static int g_tab = 0;
static float g_anim = 0.0f;
static ImVec2 g_menuMin = ImVec2(0, 0);
static ImVec2 g_menuMax = ImVec2(0, 0);

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
        g_visible = !g_visible;
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
    ImGui::SetNextWindowSize(ImVec2(620.0f * scale, 500.0f * scale), ImGuiCond_Always);

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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 2.0f);

    bool open = g_visible;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoTitleBar;
    if (ImGui::Begin("Azyre SDK", g_visible ? &open : nullptr, flags))
    {
        theme::apply();
        ImVec2 winPos = ImGui::GetWindowPos();
        g_menuMin = winPos;
        g_menuMax = ImVec2(winPos.x + ImGui::GetWindowSize().x, winPos.y + ImGui::GetWindowSize().y);

        ImGui::PushStyleColor(ImGuiCol_Text, theme::Accent);
        ImGui::Text("AZYRE");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("1.1.0  Bedrock Win10 DX11");

        bool inWorld = Game::get().localFound();
        ImGui::SameLine(ImGui::GetWindowWidth() - 150.0f);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              inWorld ? ImVec4(0.45f, 0.85f, 0.55f, 1.0f)
                                      : ImVec4(0.55f, 0.58f, 0.62f, 1.0f));
        ImGui::Text(inWorld ? "IN WORLD" : "MENU");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("X"))
            open = false;

        ImGui::PushStyleColor(ImGuiCol_Separator,
                              ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z, 0.40f));
        ImGui::Separator();
        ImGui::PopStyleColor();

        static int lastTabShown = -1;
        static float tabAnim = 1.0f;
        if (g_tab != lastTabShown)
        {
            lastTabShown = g_tab;
            tabAnim = 0.0f;
        }
        tabAnim = clampf(tabAnim + dt * 6.0f, 0.0f, 1.0f);
        float tabEase = widgets::EaseOutExpo(tabAnim);

        const float sideW = 150.0f;
        const float tabH = 30.0f;
        const float tabGap = 5.0f;
        ImVec2 areaBase = ImGui::GetCursorPos();

        float footerH = 104.0f;
        float availH = ImGui::GetContentRegionAvail().y;
        float colsH = availH - footerH;
        if (colsH < 150.0f)
            colsH = 150.0f;

        static float indY = -1.0f;
        if (indY < 0.0f)
            indY = areaBase.y;
        float targetY = areaBase.y + g_tab * (tabH + tabGap);
        indY += (targetY - indY) * clampf(dt * 14.0f, 0.0f, 1.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, winAlpha * (0.40f + 0.60f * tabEase));
        ImGui::Indent((1.0f - tabEase) * 14.0f);

        ImGui::SetCursorPos(areaBase);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 8.0f));
        if (ImGui::BeginChild("##side", ImVec2(sideW, colsH), false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            const char* tabs[] = {"Combat", "Movement", "Visuals", "Misc"};
            ImGuiWindow* sideWnd = ImGui::GetCurrentWindow();
            float bbW = ImGui::GetContentRegionAvail().x;

            for (int i = 0; i < 4; ++i)
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
                    g_tab = i;

                bool act = g_tab == i;
                ImDrawList* d = sideWnd->DrawList;
                if (act)
                    d->AddRectFilled(bb.Min, bb.Max, theme::AccentU32(0.16f), 2.0f);
                else if (hovered)
                    d->AddRectFilled(bb.Min, bb.Max, IM_COL32(255, 255, 255, 14), 2.0f);

                const char* text = tabs[i];
                ImVec2 ts = ImGui::CalcTextSize(text);
                ImVec2 textPos = ImVec2(bb.Min.x + 10.0f,
                                        bb.Min.y + (tabH - ts.y) * 0.5f);
                ImU32 textCol = act ? theme::AccentU32()
                                    : ImGui::GetColorU32(ImGuiCol_Text);
                d->AddText(textPos, textCol, text);
            }

            ImVec2 sPos = ImGui::GetWindowPos();
            float localInd = indY - areaBase.y;
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(sPos.x + sideW - 14.0f, sPos.y + localInd + 5.0f),
                ImVec2(sPos.x + sideW - 11.0f, sPos.y + localInd + tabH - 5.0f),
                theme::AccentU32(), 1.0f);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::SameLine(0.0f, 6.0f);

        if (ImGui::BeginChild("##modlist", ImVec2(ImGui::GetContentRegionAvail().x, colsH), false))
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

            ImVec2 childPos = ImGui::GetWindowPos();
            Module* binding = mgr.binding();
            std::vector<Module*> modules = mgr.byCategory((Category)g_tab);
            for (Module* m : modules)
            {
                ImVec2 rowMin = ImGui::GetCursorPos();
                ImGui::PushID(m);

                bool enabled = m->enabled();
                if (enabled)
                    ImGui::PushStyleColor(ImGuiCol_Text, theme::Accent);
                if (ImGui::Checkbox(m->name().c_str(), &enabled))
                    m->setEnabled(enabled);
                if (enabled)
                    ImGui::PopStyleColor();
                if (ImGui::IsItemHovered() && !m->description().empty())
                    ImGui::SetTooltip("%s", m->description().c_str());

                ImGui::SameLine(ImGui::GetWindowWidth() - 120.0f);
                char label[64];
                if (binding == m)
                    snprintf(label, sizeof(label), "...");
                else
                    snprintf(label, sizeof(label), "[%s]", keyName(m->keybind()));
                if (ImGui::SmallButton(label))
                {
                    if (mgr.binding() == m)
                        mgr.setBinding(nullptr);
                    else
                        mgr.setBinding(m);
                }

                if (m->enabled() && m->hasSettings())
                {
                    ImGui::Indent(14.0f);
                    ImGui::PushStyleColor(ImGuiCol_Separator,
                                          ImVec4(theme::Accent.x, theme::Accent.y,
                                                 theme::Accent.z, 0.35f));
                    ImGui::Separator();
                    ImGui::PopStyleColor();
                    m->drawSettings();
                    ImGui::Separator();
                    ImGui::Unindent(14.0f);
                }

                ImGui::PopID();
                ImVec2 rowMax(rowMin.x, ImGui::GetCursorPos().y);

                static std::map<const Module*, float> hoverAnim;
                ImVec2 sMin = childPos + rowMin;
                ImVec2 sMax = childPos + rowMax;
                bool hov = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(sMin, sMax, false);
                float ha = hoverAnim[m];
                ha += ((hov ? 1.0f : 0.0f) - ha) * clampf(dt * 14.0f, 0.0f, 1.0f);
                hoverAnim[m] = ha;
                if (ha > 0.01f)
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        sMin, sMax, IM_COL32(255, 255, 255, (int)(13.0f * ha)), 2.0f);
            }

            if (modules.empty())
                ImGui::TextDisabled("No modules in this category");
        }
        ImGui::EndChild();

        ImGui::Unindent((1.0f - tabEase) * 14.0f);
        ImGui::PopStyleVar();

        ImVec2 dividerTop(winPos.x + areaBase.x + sideW + 3.0f, winPos.y + areaBase.y - 4.0f);
        ImVec2 dividerBot(winPos.x + areaBase.x + sideW + 3.0f, winPos.y + areaBase.y + colsH + 4.0f);
        ImGui::GetWindowDrawList()->AddLine(dividerTop, dividerBot, theme::AccentU32(0.30f), 1.0f);

        ImGui::SetCursorPos(ImVec2(areaBase.x, areaBase.y + colsH + 6.0f));

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
        ImGui::Separator();

        if (mgr.binding())
            ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f),
                               "Binding: press a key | ESC clears | click '...' to cancel...");
        else
            ImGui::TextDisabled("INSERT: show/hide menu");

        ImGui::End();
    }

    ImGui::PopStyleVar(5);

    if (!open)
        g_visible = false;
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

        d->AddRectFilled(ImVec2(x0, y - h), ImVec2(x0 + w, y),
                         IM_COL32(10, 12, 15, (int)(210 * a)), 3.0f);
        d->AddRectFilled(ImVec2(x0, y - h), ImVec2(x0 + 3.5f, y),
                         it->enabled ? theme::AccentU32(a)
                                     : IM_COL32(150, 155, 160, (int)(255 * a)),
                         3.0f);
        d->AddText(ImVec2(x0 + 12.0f, y - h + 6.0f),
                   IM_COL32(240, 242, 245, (int)(255 * a)), buf);
        y -= h + 6.0f;
    }
}

}
