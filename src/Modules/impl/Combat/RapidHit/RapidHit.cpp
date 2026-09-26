#include "RapidHit.hpp"

#include "Framework/Log.hpp"

#include <imgui.h>
#include <cstring>

namespace mc {

RapidHit::RapidHit()
    : Module("RapidHit", "Bypasses the game attack cooldown", Category::Combat, 0)
{
    markHasSettings();
}

void RapidHit::onEnable()
{
    if (!site_)
    {
        static const unsigned char pattern[] = {0x0F, 0xB6, 0x4F, 0x78, 0x8B, 0x44, 0x24,
                                                0x60, 0x84, 0xC9, 0x0F, 0x84};
        site_ = patch::patternScanImage(pattern, sizeof(pattern));
        if (site_)
            MC_LOG("[RapidHit] Pattern found at 0x%p", (void*)site_);
        else
            MC_LOG_ERROR("[RapidHit] Pattern not found");
    }

    if (!site_)
        return;
    if (!patch_.init(site_, 8))
        return;
    if (!patch_.allocCave())
        return;

    unsigned char shell[64] = {};
    int p = 0;

    shell[p++] = 0x0F;
    shell[p++] = 0xB6;
    shell[p++] = 0x4F;
    shell[p++] = 0x79;

    shell[p++] = 0x8B;
    shell[p++] = 0x44;
    shell[p++] = 0x24;
    shell[p++] = 0x60;

    shell[p++] = 0xE9;
    uintptr_t retAddr = patch_.addr + 8;
    int32_t relBack = (int32_t)(retAddr - (patch_.caveAddr() + p + 4));
    memcpy(shell + p, &relBack, 4);
    p += 4;

    patch_.writeCave(0, shell, (size_t)p);

    if (patch_.enableJmpToCave())
        MC_LOG("[RapidHit] Patched at 0x%p ([rdi+78] -> [rdi+79])", (void*)patch_.addr);
}

void RapidHit::onDisable()
{
    patch_.disable();
}

void RapidHit::onShutdown()
{
    patch_.disable();
    patch_.freeCave();
}

void RapidHit::drawSettings()
{
    if (site_)
        ImGui::TextDisabled("Site: 0x%llX%s", (unsigned long long)site_,
                            patch_.enabled ? " (patched)" : "");
    else
        ImGui::TextDisabled("Site: searching pattern...");
}

MC_REGISTER_MODULE(RapidHit);

}
