#include "AutoSprint.hpp"

#include "Framework/Log.hpp"

#include <imgui.h>
#include <cstring>

namespace mc {

AutoSprint::AutoSprint()
    : Module("AutoSprint", "Forces sprint (state 6)", Category::Movement, 0)
{
    markHasSettings();
}

void AutoSprint::onEnable()
{
    if (!site_)
    {
        static const unsigned char pattern[] = {0x0F, 0xB6, 0x41, 0x63, 0x48, 0x8D, 0x2D,
                                                0x39, 0xE0, 0xC3, 0x00};
        site_ = patch::patternScanImage(pattern, sizeof(pattern));
        if (site_)
            MC_LOG("[AutoSprint] Pattern found at 0x%p", (void*)site_);
        else
            MC_LOG_ERROR("[AutoSprint] Pattern not found");
    }

    if (!site_)
        return;
    if (!patch_.init(site_, 11))
        return;
    if (!patch_.allocCave())
        return;

    unsigned char shell[64] = {};
    int p = 0;

    shell[p++] = 0xB8;
    int sprintValue = 6;
    memcpy(shell + p, &sprintValue, 4);
    p += 4;

    shell[p++] = 0xE9;
    uintptr_t retAddr = patch_.addr + 11;
    int32_t relBack = (int32_t)(retAddr - (patch_.caveAddr() + p + 4));
    memcpy(shell + p, &relBack, 4);
    p += 4;

    patch_.writeCave(0, shell, (size_t)p);

    if (patch_.enableJmpToCave())
        MC_LOG("[AutoSprint] Patched at 0x%p", (void*)patch_.addr);
}

void AutoSprint::onDisable()
{
    patch_.disable();
}

void AutoSprint::onShutdown()
{
    patch_.disable();
    patch_.freeCave();
}

void AutoSprint::drawSettings()
{
    if (site_)
        ImGui::TextDisabled("Site: 0x%llX%s", (unsigned long long)site_,
                            patch_.enabled ? " (patched)" : "");
    else
        ImGui::TextDisabled("Site: searching pattern...");
}

MC_REGISTER_MODULE(AutoSprint);

}
