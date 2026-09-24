#include "FullBright.hpp"

#include "Framework/Log.hpp"

#include <imgui.h>
#include <cstring>

namespace mc {

FullBright::FullBright()
    : Module("FullBright", "Brillo maximo en zonas oscuras", Category::Visuals, 0)
{
    markHasSettings();
}

void FullBright::onEnable()
{
    if (!site_)
    {
        static const unsigned char pattern[] = {0xF3, 0x0F, 0x10, 0x80, 0xA0, 0x01, 0x00, 0x00};
        site_ = patch::patternScanImage(pattern, sizeof(pattern));
        if (site_)
            MC_LOG("[FullBright] Pattern found at 0x%p", (void*)site_);
        else
            MC_LOG_ERROR("[FullBright] Pattern not found");
    }

    if (!site_)
        return;
    if (!patch_.init(site_, 8))
        return;
    if (!patch_.allocCave())
        return;

    float value = 100.0f;
    unsigned char shell[64] = {};
    int p = 0;

    shell[p++] = 0xB8;
    memcpy(shell + p, &value, 4);
    p += 4;

    shell[p++] = 0x66;
    shell[p++] = 0x0F;
    shell[p++] = 0x6E;
    shell[p++] = 0xC0;

    shell[p++] = 0xE9;
    uintptr_t retAddr = patch_.addr + 8;
    int32_t relBack = (int32_t)(retAddr - (patch_.caveAddr() + p + 4));
    memcpy(shell + p, &relBack, 4);
    p += 4;

    patch_.writeCave(0, shell, (size_t)p);

    if (patch_.enableJmpToCave())
        MC_LOG("[FullBright] Patched at 0x%p", (void*)patch_.addr);
}

void FullBright::onDisable()
{
    patch_.disable();
}

void FullBright::onShutdown()
{
    patch_.disable();
    patch_.freeCave();
}

void FullBright::drawSettings()
{
    if (site_)
        ImGui::TextDisabled("Site: 0x%llX%s", (unsigned long long)site_,
                            patch_.enabled ? " (patched)" : "");
    else
        ImGui::TextDisabled("Site: searching pattern...");
}

MC_REGISTER_MODULE(FullBright);

}
