#include "Reach.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Memory.hpp"
#include "SDK/Offsets.hpp"

#include <imgui.h>
#include <cmath>

namespace mc {

Reach::Reach()
    : Module("Reach", "Extended attack reach (global float)", Category::Combat, 0)
{
    markHasSettings();
}

void Reach::writeReach()
{
    uintptr_t addr = mem::resolve(off::misc::Reach);
    if (mem::isReadable(addr, sizeof(float)))
        mem::writeImage<float>(addr, reach_);
}

void Reach::onEnable()
{
    uintptr_t addr = mem::resolve(off::misc::Reach);
    if (mem::isReadable(addr, sizeof(float)))
    {
        saved_ = mem::read<float>(addr);
        savedValid_ = std::isfinite(saved_);
    }
    writeReach();
}

void Reach::onTick()
{
    writeReach();
}

void Reach::onDisable()
{
    if (savedValid_)
    {
        uintptr_t addr = mem::resolve(off::misc::Reach);
        if (mem::isReadable(addr, sizeof(float)))
            mem::writeImage<float>(addr, saved_);
    }
}

void Reach::drawSettings()
{
    widgets::CSlider("Reach##rc", &reach_, 3.0f, 15.0f, "%.1f");
    ImGui::TextDisabled("Site: base+0x%llX", (unsigned long long)off::misc::Reach);
}

MC_REGISTER_MODULE(Reach);

}
