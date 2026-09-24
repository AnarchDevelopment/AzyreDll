#include "AntiBot.hpp"

#include "Features/BotFilter.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>

namespace mc {

AntiBot::AntiBot()
    : Module("AntiBot", "Filtra bots, NPCs y hologramas de ESP/Aim", Category::Misc, 0)
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

MC_REGISTER_MODULE(AntiBot);

}
