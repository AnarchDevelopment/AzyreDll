#include "Reach.hpp"
#include "Features/ReachOverride.hpp"
#include "GUI/Widgets.hpp"
#include "SDK/Engine/Memory/Offsets.hpp"

#include <imgui.h>

namespace mc {

Reach::Reach()
    : Module("Reach", "Extended attack reach (global float)", Category::Combat, 0)
{
    markHasSettings();
}

void Reach::onEnable()
{
    reach_override::update(reach_override::Owner::ReachModule, reach_);
}

void Reach::onTick()
{
    reach_override::update(reach_override::Owner::ReachModule, reach_);
}

void Reach::onDisable()
{
    reach_override::clear(reach_override::Owner::ReachModule);
}

void Reach::drawSettings()
{
    widgets::CSlider("Reach##rc", &reach_, 3.0f, 15.0f, "%.1f");
    ImGui::TextDisabled("Site: base+0x%llX", (unsigned long long)off::misc::Reach);
}

MC_REGISTER_MODULE(Reach);

}
