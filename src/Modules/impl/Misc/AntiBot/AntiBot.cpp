#include "AntiBot.hpp"

#include "Features/BotFilter.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>

namespace mc {

AntiBot::AntiBot()
    : Module("AntiBot", "Filters bots, NPCs and holograms from ESP/Aim", Category::Misc, 0)
{
    markHasSettings();
    setEnabled(true);
}

void AntiBot::onEnable()
{
    Game::get().setBotFilter(true);
}

void AntiBot::onDisable()
{
    Game::get().setBotFilter(false);
}

void AntiBot::drawSettings()
{
    bool changed = false;
    changed = ImGui::Checkbox("Filter name keywords##ab", &botfilter::g_filterKeywords) || changed;
    changed = ImGui::Checkbox("Filter invalid names##ab", &botfilter::g_filterInvalidNames) || changed;
    if (changed)
        Game::get().refreshBotFilter();

    ImGui::TextDisabled("Applies to ESP, Tracers, NameTags, AimAssist, TargetHUD");
}

nlohmann::json AntiBot::saveSettings() const
{
    return nlohmann::json{
        {"keywords", botfilter::g_filterKeywords},
        {"invalidNames", botfilter::g_filterInvalidNames},
    };
}

void AntiBot::loadSettings(const nlohmann::json& j)
{
    botfilter::g_filterKeywords = j.value("keywords", botfilter::g_filterKeywords);
    botfilter::g_filterInvalidNames = j.value("invalidNames", botfilter::g_filterInvalidNames);
}

MC_REGISTER_MODULE(AntiBot);

}
