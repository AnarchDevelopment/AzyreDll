#include "TriggerBot.hpp"
#include "Framework/Math.hpp"
#include "Features/Friends.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Widgets.hpp"
#include "Input/SendClick.hpp"
#include "SDK/Classes/MoveInputHandler.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

#include <cmath>

namespace mc {

namespace {

uint32_t g_rng = 0x2545F491u;

uint32_t randNext()
{
    uint32_t x = g_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng = x;
    return x;
}

bool rollChance(int pct)
{
    if (pct >= 100)
        return true;
    if (pct <= 0)
        return false;
    return (randNext() % 100) < (uint32_t)pct;
}

}

TriggerBot::TriggerBot()
    : Module("TriggerBot", "Automatically attacks when your crosshair is over a player.", Category::Combat, 0)
{
    markHasSettings();
}

void TriggerBot::onEnable()
{
    g_rng = (uint32_t)GetTickCount() ^ 0x2545F491u;
    nextClick_ = 0;
}

void TriggerBot::onFrame(float)
{
    if (menu::visible() || ImGui::GetIO().WantTextInput)
        return;

    ULONGLONG now = GetTickCount64();
    if (nextClick_ != 0 && now < nextClick_)
        return;

    // Si el usuario mantiene pulsado el ataque, sus clicks reales mandan.
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
        return;

    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    if (!local.valid())
        return;

    Vec3 eye = local.eyePos();
    float myYaw = local.yaw();
    float myPitch = local.pitch();

    bool onTarget = false;

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
        if (std::fabs(yawDiff) > fov_ || std::fabs(pitchDiff) > fov_)
            continue;

        onTarget = true;
        break;
    }

    if (!onTarget)
        return;

    if (!rollChance(chance_))
        return;

    bool wantNative = method_ == 0 || method_ == 1;
    bool wantPhysical = method_ == 0 || method_ == 2;

    if (wantNative)
    {
        MoveInputHandler input(local.moveInputAddr());
        if (input.valid())
        {
            input.clickFast();
            input.click();
        }
    }

    if (wantPhysical)
    {
        sendLeftDown();
        sendLeftUp();
    }

    ++clicks_;
    nextClick_ = now + (ULONGLONG)delay_;
}

void TriggerBot::drawSettings()
{
    const char* methods[] = {"Auto (all)", "Native (InputHandler)", "Physical (SendInput)"};
    widgets::CCombo("Method##tb", &method_, methods, 3);
    widgets::CSlider("Range##tb", &range_, 1.0f, 8.0f, "%.1f");
    widgets::CSlider("FOV##tb", &fov_, 0.5f, 30.0f, "%.1f");
    widgets::CSlider("Delay##tb", &delay_, 0, 500, "%d ms");
    widgets::CSlider("Chance##tb", &chance_, 1, 100, "%d%%");

    bool inputOk = false;
    if (Game::get().localFound())
    {
        LocalPlayer local = Game::get().localPlayer();
        if (local.valid())
            inputOk = MoveInputHandler(local.moveInputAddr()).valid();
    }
    ImGui::TextDisabled("Clicks: %u   Input: %s", clicks_, inputOk ? "valid" : "invalid");
}

MC_REGISTER_MODULE(TriggerBot);

}
