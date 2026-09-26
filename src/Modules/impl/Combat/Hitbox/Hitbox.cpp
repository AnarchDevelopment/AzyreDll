#include "Hitbox.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Log.hpp"
#include "Framework/Memory.hpp"
#include "SDK/Offsets.hpp"

#include <imgui.h>
#include <cstring>

namespace mc {

Hitbox::Hitbox()
    : Module("Hitbox", "Expands the attack hitbox", Category::Combat, 0)
{
    markHasSettings();
}

void Hitbox::onEnable()
{
    if (!patch_.init(mem::resolve(off::misc::HitboxExpand), 8))
    {
        MC_LOG_ERROR("[Hitbox] init failed");
        return;
    }
    if (!patch_.allocCave())
        return;

    unsigned char shell[64] = {};
    int p = 0;

    shell[p++] = 0xB8;
    memcpy(shell + p, &value_, 4);
    p += 4;

    shell[p++] = 0x89;
    shell[p++] = 0x81;
    shell[p++] = 0xD0;
    shell[p++] = 0x00;
    shell[p++] = 0x00;
    shell[p++] = 0x00;

    memcpy(shell + p, patch_.backup, 8);
    p += 8;

    shell[p++] = 0xE9;
    uintptr_t retAddr = patch_.addr + 8;
    int32_t relBack = (int32_t)(retAddr - (patch_.caveAddr() + p + 4));
    memcpy(shell + p, &relBack, 4);
    p += 4;

    patch_.writeCave(0, shell, (size_t)p);

    if (patch_.enableJmpToCave())
        MC_LOG("[Hitbox] Patched at 0x%p cave 0x%p", (void*)patch_.addr, (void*)patch_.caveAddr());
}

void Hitbox::onDisable()
{
    patch_.disable();
}

void Hitbox::onShutdown()
{
    patch_.disable();
    patch_.freeCave();
}

void Hitbox::drawSettings()
{
    if (widgets::CSlider("Size##hb", &value_, 0.6f, 10.0f, "%.2f"))
    {
        if (patch_.enabled && patch_.cave)
            patch_.writeCave(1, &value_, 4);
    }

    if (patch_.addr)
        ImGui::TextDisabled("Site: 0x%llX%s", (unsigned long long)patch_.addr,
                            patch_.enabled ? " (patched)" : "");
    else
        ImGui::TextDisabled("Site: base+0x%llX", (unsigned long long)off::misc::HitboxExpand);
}

MC_REGISTER_MODULE(Hitbox);

}
