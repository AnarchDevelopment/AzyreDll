#include "NoHurtCam.hpp"

#include "Framework/Log.hpp"

#include <imgui.h>

namespace mc {

NoHurtCam::NoHurtCam()
    : Module("NoHurtCam", "Elimina el temblor de camara al recibir dano", Category::Visuals, 0)
{
    markHasSettings();
}

void NoHurtCam::onEnable()
{
    if (!site_)
    {
        static const unsigned char pattern[] = {0x66, 0x0F, 0x6E, 0xBB, 0x24, 0x02, 0x00, 0x00};
        site_ = patch::patternScanImage(pattern, sizeof(pattern));
        if (site_)
            MC_LOG("[NoHurtCam] Pattern found at 0x%p", (void*)site_);
        else
            MC_LOG_ERROR("[NoHurtCam] Pattern not found");
    }

    if (!site_)
        return;
    if (!patch_.init(site_, 8))
        return;

    static const unsigned char replacement[] = {0x0F, 0x57, 0xFF, 0x90, 0x90, 0x90, 0x90, 0x90};
    if (patch_.enableInline(replacement, sizeof(replacement)))
        MC_LOG("[NoHurtCam] Patched (xorps xmm7,xmm7)");
}

void NoHurtCam::onDisable()
{
    patch_.disable();
}

void NoHurtCam::onShutdown()
{
    patch_.disable();
}

void NoHurtCam::drawSettings()
{
    if (site_)
        ImGui::TextDisabled("Site: 0x%llX%s", (unsigned long long)site_,
                            patch_.enabled ? " (patched)" : "");
    else
        ImGui::TextDisabled("Site: searching pattern...");
}

MC_REGISTER_MODULE(NoHurtCam);

}
