#include "AimAssist.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "Features/Friends.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

namespace mc {

AimAssist::AimAssist()
    : Module("AimAssist", "Smooths your aim towards the nearest player", Category::Combat, 'R')
{
    markHasSettings();
}

void AimAssist::onFrame(float dt)
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    Vec3 eye = local.eyePos();
    float myYaw = local.yaw();
    float myPitch = local.pitch();

    bool aiming = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (onlyWeapons_ && !aiming)
        return;

    float bestScore = range_;
    float targetYaw = 0.0f;
    float targetPitch = 0.0f;
    bool found = false;

    for (const Actor& other : game.remotePlayers())
    {
        if (!other.valid())
            continue;

        Vec3 targetEye = other.eyePos();
        float dist = distance3D(eye, targetEye);
        if (dist > range_ || dist < 0.1f)
            continue;
        if (friends::isFriend(other.name()))
            continue;

        AimAngles angles = calcAngles(eye, targetEye);
        float yawDiff = angleDiff(myYaw, angles.yaw);
        float pitchDiff = angles.pitch - myPitch;
        if (std::fabs(yawDiff) > fov_ * 0.5f || std::fabs(pitchDiff) > fov_ * 0.5f)
            continue;

        if (dist < bestScore)
        {
            bestScore = dist;
            targetYaw = angles.yaw;
            targetPitch = angles.pitch;
            found = true;
        }
    }

    if (!found)
        return;

    float tH = clampf(speedH_ * dt * 60.0f, 0.0f, 1.0f);
    float tV = clampf(speedV_ * dt * 60.0f, 0.0f, 1.0f);
    float newYaw = myYaw + angleDiff(myYaw, targetYaw) * tH;
    float newPitch = myPitch + (targetPitch - myPitch) * tV;
    local.setYaw(wrapDegrees(newYaw));
    local.setPitch(clampf(newPitch, -90.0f, 90.0f));
}

void AimAssist::drawSettings()
{
    widgets::CSlider("Range##aa", &range_, 1.0f, 8.0f, "%.1f");
    widgets::CSlider("FOV##aa", &fov_, 0.0f, 360.0f, "%.0f");
    widgets::CSlider("Horizontal Speed##aa", &speedH_, 0.02f, 1.0f, "%.2f");
    widgets::CSlider("Vertical Speed##aa", &speedV_, 0.02f, 1.0f, "%.2f");
    ImGui::Checkbox("Only while attacking##aa", &onlyWeapons_);
}

MC_REGISTER_MODULE(AimAssist);

}
