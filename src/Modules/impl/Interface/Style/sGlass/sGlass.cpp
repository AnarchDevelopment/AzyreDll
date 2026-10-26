#include "sGlass.hpp"

#include "GUI/Menu.hpp"
#include "GUI/Theme.hpp"
#include "Modules/ModuleManager.hpp"
#include "Modules/impl/Interface/Interface.hpp"
#include "Render/BlurHelper/BlurHelper.hpp"
#include "Config/Config.hpp"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace mc::gui::sGlass {
namespace {

constexpr float kRadius = 20.0f;
const Category kCategories[] = {Category::Combat, Category::Movement, Category::Visuals,
                                Category::Misc, Category::Scripting, Category::Interface};

float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }

float easeOutExpo(float t)
{
    t = clamp01(t);
    return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
}

bool containsCI(const std::string& value, const char* query)
{
    if (!query || !*query)
        return true;
    std::string a = value;
    std::string b = query;
    for (char& c : a) c = (char)tolower((unsigned char)c);
    for (char& c : b) c = (char)tolower((unsigned char)c);
    return a.find(b) != std::string::npos;
}

ImVec4 tint(float alpha)
{
    return ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z, alpha);
}

} // namespace

void render(float anim)
{
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 screen = io.DisplaySize;
    if (screen.x < 420.0f || screen.y < 320.0f)
        return;

    static int activeCategory = 0;
    static Module* selectedModule = nullptr;
    static char search[96] = {};
    static float openProgress = 0.0f;
    static float categoryProgress = 1.0f;
    static float glowPhase = 0.0f;
    static std::map<const Module*, float> cardReveal;
    static float modulesScroll = 0.0f;
    static float modulesScrollTarget = 0.0f;
    static bool scrollInitialized = false;
    static int previousCategory = 0;
    static std::string previousSearch;
    const float dt = std::min(io.DeltaTime, 0.05f);
    const float target = menu::visible() ? 1.0f : 0.0f;
    const float transitionSpeed = gui::glassMotion() == 2 ? 9.0f : 6.0f;
    const float transitionStep = dt * transitionSpeed;
    if (openProgress < target)
        openProgress = std::min(target, openProgress + transitionStep);
    else
        openProgress = std::max(target, openProgress - transitionStep);
    categoryProgress += (1.0f - categoryProgress) * std::min(1.0f, dt * 10.0f);
    glowPhase += dt * 0.22f;
    const float fade = easeOutExpo(openProgress);
    (void)anim;
    if (fade < 0.002f)
        return;

    const float maxW = std::max(420.0f, screen.x - 40.0f);
    const float maxH = std::max(360.0f, screen.y - 40.0f);
    const float panelW = std::min(1060.0f, maxW);
    const float panelH = std::min(700.0f, maxH);
    const float motionScale = gui::glassMotion() == 2 ? 1.0f : 0.94f + 0.06f * fade;
    const float panelAlpha = gui::glassOpacity();
    const ImVec2 center(screen.x * 0.5f, screen.y * 0.5f);
    ImVec2 panelPos(center.x - panelW * motionScale * 0.5f,
                    center.y - panelH * motionScale * 0.5f + (1.0f - fade) * 18.0f);
    ImVec2 panelSize(panelW * motionScale, panelH * motionScale);
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    // Frosted backdrop with restrained moving color blooms.
    bg->AddRectFilled(ImVec2(0, 0), screen, IM_COL32(4, 7, 14, (int)(142.0f * fade)));
    if (blur::ready() && blur::srv())
        bg->AddImage(ImTextureRef((void*)blur::srv()), ImVec2(0, 0), screen,
                     ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, (int)(150.0f * fade)));
    const float bloomX = screen.x * (0.27f + 0.045f * std::sin(glowPhase));
    const float bloomY = screen.y * (0.35f + 0.035f * std::cos(glowPhase * 0.8f));
    for (int i = 5; i >= 0; --i)
    {
        const float r = 90.0f + i * 28.0f;
        bg->AddCircleFilled(ImVec2(bloomX, bloomY), r,
                            theme::AccentU32((0.018f + (5 - i) * 0.008f) * fade), 48);
    }

    ImGui::SetNextWindowPos(panelPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(panelSize, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fade);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 7.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.93f, 0.96f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.61f, 0.68f, 0.77f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.25f, 0.34f, 0.54f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.29f, 0.38f, 0.50f, 0.72f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, tint(0.78f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.15f, 0.22f, 0.68f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.16f, 0.23f, 0.33f, 0.78f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.18f, 0.27f, 0.39f, 0.88f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.84f, 0.92f, 1.0f, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, theme::Accent);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, theme::Accent);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar;
    if (ImGui::Begin("##AzyreGlass", nullptr, flags))
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        const ImVec2 end(p.x + size.x, p.y + size.y);
        const int glassA = (int)(255.0f * panelAlpha);
        draw->AddRectFilled(p, end, IM_COL32(13, 20, 31, glassA), kRadius);
        draw->AddRectFilledMultiColor(ImVec2(p.x + 1.0f, p.y + 1.0f),
                                      ImVec2(end.x - 1.0f, p.y + 116.0f),
                                      IM_COL32(255, 255, 255, (int)(20.0f * fade)),
                                      IM_COL32(255, 255, 255, (int)(4.0f * fade)),
                                      IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
        draw->AddRect(p, end, IM_COL32(230, 242, 255, (int)(50.0f * fade)), kRadius, 0, 1.0f);
        draw->AddRectFilled(ImVec2(p.x + 24.0f, p.y + 73.0f),
                            ImVec2(p.x + size.x - 24.0f, p.y + 74.0f),
                            IM_COL32(255, 255, 255, (int)(18.0f * fade)));

        ImGui::SetCursorPos(ImVec2(28.0f, 20.0f));
        ImGui::TextColored(theme::Accent, "A Z Y R E");
        ImGui::SameLine(132.0f);
        ImGui::TextDisabled("GLASS INTERFACE");
        ImGui::SameLine(size.x - 160.0f);
        ImGui::TextDisabled("INSERT TO HIDE");
        ImGui::SetCursorPos(ImVec2(26.0f, 88.0f));

        const float contentH = std::max(160.0f, size.y - 112.0f);
        const float bodyW = std::max(300.0f, size.x - 48.0f);
        const float sideW = 164.0f;
        const bool showInspector = size.x >= 780.0f;
        const float inspectorW = showInspector ? std::max(240.0f, std::min(286.0f, size.x * 0.27f)) : 0.0f;
        const float columnGap = 14.0f;
        const float modulesW = std::max(120.0f, bodyW - sideW - (showInspector ? inspectorW + columnGap * 2.0f : columnGap));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
        ImGui::BeginChild("##glass_sidebar", ImVec2(sideW, contentH), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::TextDisabled("WORKSPACE");
        ImGui::Spacing();
        for (int i = 0; i < (int)(sizeof(kCategories) / sizeof(kCategories[0])); ++i)
        {
            const int count = (int)ModuleManager::get().byCategory(kCategories[i]).size();
            char label[96];
            snprintf(label, sizeof(label), "%s   %02d##glass_cat_%d", categoryName(kCategories[i]), count, i);
            ImGui::PushStyleColor(ImGuiCol_Button,
                i == activeCategory ? tint(0.25f) : ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tint(0.16f));
            if (ImGui::Button(label, ImVec2(-1.0f, 36.0f)))
            {
                activeCategory = i;
                categoryProgress = 0.0f;
            }
            ImGui::PopStyleColor(2);
            if (i == activeCategory)
            {
                const ImVec2 a = ImGui::GetItemRectMin();
                const ImVec2 b = ImGui::GetItemRectMax();
                const float indicatorH = (b.y - a.y - 14.0f) * std::max(0.0f, categoryProgress);
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x, a.y + 7.0f),
                    ImVec2(a.x + 3.0f, a.y + 7.0f + indicatorH), theme::AccentU32(0.95f), 2.0f);
            }
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("PRESENTATION");
        ImGui::TextWrapped("Soft blur, layered surfaces, and quiet motion keep the game visible behind your tools.");
        ImGui::EndChild();

        ImGui::SameLine(0.0f, columnGap);
        ImGui::BeginChild("##glass_modules", ImVec2(modulesW, contentH),
                          false, ImGuiWindowFlags_AlwaysVerticalScrollbar |
                                 ImGuiWindowFlags_NoScrollWithMouse);
        if (ImGui::IsWindowHovered())
        {
            if (!scrollInitialized)
            {
                modulesScrollTarget = ImGui::GetScrollY();
                scrollInitialized = true;
            }
            if (io.MouseWheel != 0.0f)
                modulesScrollTarget -= io.MouseWheel * 74.0f;
        }
        const float maxScroll = ImGui::GetScrollMaxY();
        modulesScrollTarget = std::max(0.0f, std::min(modulesScrollTarget, maxScroll));
        modulesScroll = ImGui::GetScrollY();
        const float scrollBlend = gui::glassMotion() == 2 ? 1.0f : std::min(1.0f, dt * 12.0f);
        modulesScroll += (modulesScrollTarget - modulesScroll) * scrollBlend;
        ImGui::SetScrollY(modulesScroll);
        ImGui::Text("%s", categoryName(kCategories[activeCategory]));
        ImGui::SameLine();
        ImGui::TextDisabled(" / module library");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##glass_search", "Search modules...", search, sizeof(search));
        if (activeCategory != previousCategory || previousSearch != search)
        {
            modulesScrollTarget = 0.0f;
            modulesScroll = 0.0f;
            previousCategory = activeCategory;
            previousSearch = search;
            scrollInitialized = true;
        }
        ImGui::Spacing();

        std::vector<Module*> modules = ModuleManager::get().byCategory(kCategories[activeCategory]);
        bool hasAny = false;
        for (Module* module : modules)
        {
            if (!containsCI(module->name(), search) && !containsCI(module->description(), search))
                continue;
            hasAny = true;
            float& reveal = cardReveal[module];
            reveal += (1.0f - reveal) * std::min(1.0f, dt * 8.0f);
            ImGui::PushID(module);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, reveal);
            ImGui::PushStyleColor(ImGuiCol_ChildBg,
                selectedModule == module ? ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z, 0.10f)
                                         : ImVec4(0.78f, 0.88f, 1.0f, 0.035f));
            ImGui::BeginChild("##module_card", ImVec2(-1.0f, 96.0f), true);
            ImGui::Text("%s", module->name().c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", module->enabled() ? "ACTIVE" : "READY");
            ImGui::SameLine(std::max(160.0f, ImGui::GetWindowWidth() - 184.0f));
            if (ImGui::Button("Details##glass_details", ImVec2(72.0f, 22.0f)))
                selectedModule = module;
            ImGui::SameLine();
            const char* toggleLabel = module->enabled() ? "Disable##glass_toggle" : "Enable##glass_toggle";
            if (ImGui::Button(toggleLabel, ImVec2(82.0f, 22.0f)))
            {
                module->toggle();
                config::saveAll();
            }
            ImGui::TextWrapped("%s", module->description().c_str());
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            ImGui::PopID();
            ImGui::Spacing();
        }
        if (!hasAny)
            ImGui::TextDisabled("No modules match this search.");
        ImGui::EndChild();

        if (showInspector)
        {
            ImGui::SameLine(0.0f, columnGap);
            ImGui::BeginChild("##glass_inspector", ImVec2(inspectorW, contentH), true);
            if (selectedModule)
            {
                ImGui::TextDisabled("MODULE DETAILS");
                ImGui::Spacing();
                ImGui::TextWrapped("%s", selectedModule->name().c_str());
                ImGui::TextDisabled("%s", categoryName(selectedModule->category()));
                ImGui::Separator();
                ImGui::TextWrapped("%s", selectedModule->description().c_str());
                ImGui::Spacing();
                if (selectedModule->hasSettings())
                    selectedModule->drawSettings();
                else
                    ImGui::TextDisabled("This module has no additional settings.");
            }
            else
            {
                ImGui::TextDisabled("INSPECTOR");
                ImGui::Spacing();
                ImGui::TextWrapped("Choose a module card to see its description and settings here.");
                ImGui::Spacing();
                ImGui::TextColored(theme::Accent, "Glass / 01");
                ImGui::TextDisabled("Category transitions are fluid. Module state saves automatically.");
            }
            ImGui::EndChild();
        }
        ImGui::PopStyleVar();
    }
    ImGui::End();

    // Draw the hairline after child windows so their edges cannot cover it.
    ImGui::GetForegroundDrawList()->AddRect(panelPos,
        ImVec2(panelPos.x + panelSize.x, panelPos.y + panelSize.y),
        IM_COL32(230, 242, 255, (int)(50.0f * fade)), kRadius, 0, 1.0f);

    ImGui::PopStyleColor(11);
    ImGui::PopStyleVar(6);
    menu::renderHudChrome();
}

} // namespace mc::gui::sGlass
