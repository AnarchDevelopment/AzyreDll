#include "UnlockFPS.hpp"
#include "GUI/Widgets.hpp"

#include "Render/DX11.hpp"
#include "SDK/ChatHook.hpp"

#include <cstdio>

namespace mc {

UnlockFPS::UnlockFPS()
    : Module("UnlockFPS", "Unlocks the frame rate cap (DXGI present interval 0)",
             Category::Misc, 0)
{
    markHasSettings();
}

void UnlockFPS::onEnable()
{
    dx11::setAllowTearing(tearing_);
    dx11::setUnlockFps(true);

    if (notify_)
    {
        float hz = dx11::swapchainRefreshHz();
        bool tear = dx11::swapchainTearingSupported();
        char buf[192];
        if (hz > 0.0f)
            snprintf(buf, sizeof(buf),
                     "§7[§bUnlockFPS§7]§f FPS unlocked via DXGI — display %.0f Hz%s",
                     hz, tear ? " (tearing supported)" : "");
        else
            snprintf(buf, sizeof(buf), "§7[§bUnlockFPS§7]§f FPS unlocked via DXGI%s",
                     tear ? " (tearing supported)" : "");
        chathook::display(buf);
    }
}

void UnlockFPS::onDisable()
{
    dx11::setUnlockFps(false);
}

void UnlockFPS::onShutdown()
{
    dx11::setUnlockFps(false);
}

void UnlockFPS::drawSettings()
{
    widgets::Checkbox("Notify on enable##fps", &notify_);
    if (widgets::Checkbox("Allow tearing##fps", &tearing_))
        dx11::setAllowTearing(tearing_);

    float hz = dx11::swapchainRefreshHz();
    bool tear = dx11::swapchainTearingSupported();
    char info[96];
    snprintf(info, sizeof(info), "Display: %s   Tearing: %s",
             hz > 0.0f ? "detected" : "unknown", tear ? "yes" : "no");
    ImGui::TextDisabled("%s", info);
    ImGui::TextDisabled("Forces Present(SyncInterval=0) — vsync off");
    ImGui::TextDisabled("Also set Max framerate to Unlimited in-game");
}

MC_REGISTER_MODULE(UnlockFPS);

}
