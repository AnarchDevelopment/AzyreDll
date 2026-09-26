#include "Glide.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Log.hpp"
#include "Framework/Memory.hpp"
#include "SDK/Offsets.hpp"

#include <imgui.h>
#include <cstring>

namespace mc {

Glide::Glide()
    : Module("Glide", "Reduces falling speed", Category::Movement, 0)
{
    markHasSettings();
}

void Glide::onEnable()
{
    if (!site_)
    {
        static const unsigned char pattern[] = {0x0F, 0x28, 0xCE, 0xF3, 0x0F, 0x11, 0x47, 0x34,
                                                0xFF, 0x90, 0xC8, 0x04, 0x00, 0x00};
        uintptr_t found = patch::patternScanImage(pattern, sizeof(pattern));
        if (found)
        {
            site_ = found + 3;
            MC_LOG("[Glide] Pattern found at 0x%p", (void*)site_);
        }
        else
        {
            site_ = mem::resolve(off::misc::GlideFallback);
            MC_LOG("[Glide] Pattern not found, using fallback 0x%p", (void*)site_);
        }
    }

    if (!site_)
        return;
    if (!patch_.init(site_, 5))
        return;
    if (!patch_.allocCave())
        return;

    unsigned char shell[64] = {};
    int p = 0;

    shell[p++] = 0x0F;
    shell[p++] = 0x57;
    shell[p++] = 0xDB;

    shell[p++] = 0x0F;
    shell[p++] = 0x2F;
    shell[p++] = 0x5F;
    shell[p++] = 0x34;

    shell[p++] = 0x77;
    shell[p++] = 0x0A;

    shell[p++] = 0xF3;
    shell[p++] = 0x0F;
    shell[p++] = 0x11;
    shell[p++] = 0x47;
    shell[p++] = 0x34;

    shell[p++] = 0xE9;
    int32_t relNormal = (int32_t)((patch_.addr + 5) - (patch_.caveAddr() + p + 4));
    memcpy(shell + p, &relNormal, 4);
    p += 4;

    shell[p++] = 0xC7;
    shell[p++] = 0x47;
    shell[p++] = 0x34;
    caveSpeedOffset_ = p;
    memcpy(shell + p, &speed_, 4);
    p += 4;

    shell[p++] = 0xE9;
    int32_t relGlide = (int32_t)((patch_.addr + 5) - (patch_.caveAddr() + p + 4));
    memcpy(shell + p, &relGlide, 4);
    p += 4;

    patch_.writeCave(0, shell, (size_t)p);

    if (patch_.enableJmpToCave())
        MC_LOG("[Glide] Patched at 0x%p", (void*)patch_.addr);
}

void Glide::onDisable()
{
    patch_.disable();
}

void Glide::onShutdown()
{
    patch_.disable();
    patch_.freeCave();
}

void Glide::drawSettings()
{
    if (widgets::CSlider("Fall speed##gl", &speed_, -0.5f, 0.0f, "%.2f"))
    {
        if (patch_.enabled && patch_.cave && caveSpeedOffset_ >= 0)
            patch_.writeCave((size_t)caveSpeedOffset_, &speed_, 4);
    }

    if (site_)
        ImGui::TextDisabled("Site: 0x%llX%s", (unsigned long long)site_,
                            patch_.enabled ? " (patched)" : "");
    else
        ImGui::TextDisabled("Site: searching pattern...");
}

MC_REGISTER_MODULE(Glide);

}
