#include "Tracers.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "SDK/Camera.hpp"
#include "SDK/Game.hpp"
#include "Features/Smoothing.hpp"

namespace mc {

Tracers::Tracers()
    : Module("Tracers", "Lineas hacia los jugadores", Category::Visuals, 0)
{
    markHasSettings();
}

void Tracers::onRender()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 0.0f || sh <= 0.0f)
        return;

    if (!fovInited_)
    {
        fov_ = readGameFov();
        fovInited_ = true;
    }

    CameraView cam = getCameraView(local, game.client());
    ImU32 col = ImGui::ColorConvertFloat4ToU32(color_);
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImVec2 origin(sw * 0.5f, sh);

    for (const Actor& other : game.remotePlayers())
    {
        if (!other.valid())
            continue;

        Vec3 feet = smooth::entityPos(other.address(), other.pos());
        Vec3 s;
        if (!worldToScreen(feet, cam.pos, cam.yaw, cam.pitch, fov_, sw, sh, s))
            continue;

        if (s.x < -200.0f || s.x > sw + 200.0f || s.y < -200.0f || s.y > sh + 200.0f)
            continue;

        d->AddLine(origin, ImVec2(s.x, s.y), col, thickness_);
    }
}

void Tracers::drawSettings()
{
    widgets::CSlider("FOV##tr", &fov_, 30.0f, 130.0f, "%.0f");
    widgets::CSlider("Thickness##tr", &thickness_, 0.5f, 4.0f, "%.1f");
    ImGui::ColorEdit4("Color##tr", (float*)&color_);
}

MC_REGISTER_MODULE(Tracers);

}
