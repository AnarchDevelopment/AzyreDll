#include "sPacket.hpp"

#include "Config/Config.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Theme.hpp"
#include "GUI/Widgets.hpp"
#include "Modules/ModuleManager.hpp"
#include "Render/BlurHelper/BlurHelper.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace mc::gui::sPacket {
namespace {

// Packet shows every category at once. These map directly to categories
// available in Azyre rather than copying Packet's unrelated module list.
const Category kCategories[] = {Category::Combat, Category::Visuals, Category::Movement,
                                Category::Interface, Category::Misc, Category::Scripting};

float clamp01(float value)
{
    return std::max(0.0f, std::min(1.0f, value));
}

} // namespace

void render(float anim)
{
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 screen = io.DisplaySize;
    if (screen.x < 460.0f || screen.y < 300.0f)
        return;

    static float open = 0.0f;
    static std::map<const Module*, float> hoverAnim;
    static Module* expandedModule = nullptr;

    const float dt = std::min(io.DeltaTime, 0.05f);
    const float target = menu::visible() ? 1.0f : 0.0f;
    open += (target - open) * std::min(1.0f, dt * 10.0f);
    if (std::fabs(target - open) < 0.001f)
        open = target;
    const float fade = clamp01(open);
    (void)anim;
    if (fade < 0.002f)
        return;

    const ImVec2 origin(0.0f, 0.0f);
    menu::setWindowRect(origin, screen);

    ImDrawList* background = ImGui::GetBackgroundDrawList();
    if (blur::ready() && blur::srv())
        background->AddImage(ImTextureRef((void*)blur::srv()), origin, screen,
                             ImVec2(0, 0), ImVec2(1, 1),
                             IM_COL32(255, 255, 255, (int)(255.0f * fade)));
    background->AddRectFilled(origin, screen,
                              IM_COL32(0, 0, 0, (int)((blur::ready() ? 72.0f : 128.0f) * fade)));

    const float gap = 11.0f;
    const float margin = 18.0f;
    const float columnW = std::min(204.0f,
        (screen.x - margin * 2.0f - gap * 5.0f) / 6.0f);
    const float totalW = columnW * 6.0f + gap * 5.0f;
    const float startX = std::max(8.0f, (screen.x - totalW) * 0.5f);
    const float top = 14.0f;
    const float maxColumnH = screen.y - top - 16.0f;

    ImGui::SetNextWindowPos(origin, ImGuiCond_Always);
    ImGui::SetNextWindowSize(screen, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fade);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 11.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.89f, 0.90f, 0.92f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.54f, 0.55f, 0.58f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.025f, 0.027f, 0.031f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.085f, 0.095f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.13f, 0.14f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.16f, 0.17f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, theme::Accent);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, theme::Accent);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, theme::Accent);

    const ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoDecoration |
                                       ImGuiWindowFlags_NoSavedSettings |
                                       ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoBackground |
                                       ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("##Packet31Interface", nullptr, rootFlags))
    {
        ImDrawList* rootDraw = ImGui::GetWindowDrawList();
        for (int i = 0; i < (int)(sizeof(kCategories) / sizeof(kCategories[0])); ++i)
        {
            const Category category = kCategories[i];
            std::vector<Module*> modules = ModuleManager::get().byCategory(category);
            const float x = startX + i * (columnW + gap);

            float contentH = 40.0f + (float)modules.size() * 32.0f + 12.0f;
            if (expandedModule && expandedModule->category() == category)
                contentH += 214.0f;
            const float columnH = std::min(maxColumnH, contentH);
            const ImVec2 cardMin(x, top + (1.0f - fade) * -7.0f);
            const ImVec2 cardMax(x + columnW, cardMin.y + columnH);
            rootDraw->AddRectFilled(cardMin, cardMax, IM_COL32(10, 10, 11, (int)(242.0f * fade)), 12.0f);
            rootDraw->AddRect(cardMin, cardMax, IM_COL32(255, 255, 255, (int)(12.0f * fade)), 12.0f);

            ImGui::SetCursorScreenPos(ImVec2(x + 4.0f, top + 4.0f));
            const ImGuiWindowFlags colFlags = ImGuiWindowFlags_NoBackground |
                                              ImGuiWindowFlags_NoScrollbar;
            char childId[48];
            std::snprintf(childId, sizeof(childId), "##packet_column_%d", i);
            if (!ImGui::BeginChild(childId, ImVec2(columnW - 8.0f, columnH - 8.0f),
                                   false, colFlags))
            {
                ImGui::EndChild();
                continue;
            }

            ImDrawList* columnDraw = ImGui::GetWindowDrawList();
            ImGui::SetCursorPosY(7.0f);
            const char* title = categoryName(category);
            const float titleW = ImGui::CalcTextSize(title).x;
            ImGui::SetCursorPosX(std::max(0.0f, (columnW - 8.0f - titleW) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(245, 245, 247, 255));
            ImGui::TextUnformatted(title);
            ImGui::PopStyleColor();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);

            ImVec2 separatorA = ImGui::GetCursorScreenPos();
            columnDraw->AddLine(ImVec2(x + 13.0f, separatorA.y),
                                ImVec2(x + columnW - 13.0f, separatorA.y),
                                IM_COL32(255, 255, 255, (int)(19.0f * fade)), 1.0f);
            ImGui::Dummy(ImVec2(1.0f, 5.0f));

            for (Module* module : modules)
            {
                ImGui::PushID(module);
                const ImVec2 rowMin = ImGui::GetCursorScreenPos();
                const float rowH = 29.0f;
                ImGui::InvisibleButton("##packet_module", ImVec2(columnW - 16.0f, rowH));
                const ImVec2 rowMax = ImGui::GetItemRectMax();
                const bool hovered = ImGui::IsItemHovered();
                const float arrowW = module->hasSettings() ? 24.0f : 0.0f;
                const bool onArrow = hovered && arrowW > 0.0f &&
                                     io.MousePos.x >= rowMax.x - arrowW;
                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    if (onArrow)
                        expandedModule = (expandedModule == module) ? nullptr : module;
                    else
                    {
                        module->toggle();
                        config::saveAll();
                    }
                }

                float& hover = hoverAnim[module];
                hover += ((hovered ? 1.0f : 0.0f) - hover) * std::min(1.0f, dt * 14.0f);
                const bool enabled = module->enabled();
                const ImU32 rowBg = enabled ? theme::AccentU32(0.96f)
                    : IM_COL32(255, 255, 255, (int)(12.0f * hover * fade));
                columnDraw->AddRectFilled(rowMin, rowMax, rowBg, 1.0f);

                const char* name = module->name().c_str();
                const ImVec2 textSize = ImGui::CalcTextSize(name);
                const float textRight = module->hasSettings()
                    ? rowMax.x - 25.0f : rowMax.x;
                const float textX = std::max(rowMin.x + 2.0f,
                    rowMin.x + ((textRight - rowMin.x) - textSize.x) * 0.5f);
                const ImU32 textColor = enabled ? IM_COL32(255, 255, 255, 255)
                                                : IM_COL32(190, 191, 194, 255);
                columnDraw->AddText(ImVec2(textX, rowMin.y + (rowH - textSize.y) * 0.5f),
                                    textColor, name);
                if (module->hasSettings())
                {
                    const char* glyph = expandedModule == module ? "-" : "+";
                    const ImVec2 glyphSize = ImGui::CalcTextSize(glyph);
                    columnDraw->AddText(ImVec2(rowMax.x - 14.0f,
                        rowMin.y + (rowH - glyphSize.y) * 0.5f),
                        enabled ? IM_COL32(255, 255, 255, 230) : IM_COL32(145, 146, 150, 230), glyph);
                }
                ImGui::PopID();

                if (expandedModule == module && module->hasSettings())
                {
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 7.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 5.0f));
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 105));
                    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 18));
                    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                    char settingsId[96];
                    std::snprintf(settingsId, sizeof(settingsId), "##packet_settings_%s", module->name().c_str());
                    ImGui::BeginChild(settingsId, ImVec2(columnW - 16.0f, 204.0f), true,
                                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
                    ImGui::TextDisabled("%s", module->name().c_str());
                    ImGui::Separator();
                    ImGui::PushItemWidth(-1.0f);
                    module->drawSettings();
                    ImGui::PopItemWidth();
                    ImGui::EndChild();
                    ImGui::PopStyleColor(2);
                    ImGui::PopStyleVar(2);
                }
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(9);
    ImGui::PopStyleVar(6);
    menu::renderHudChrome();
}

} // namespace mc::gui::sPacket
