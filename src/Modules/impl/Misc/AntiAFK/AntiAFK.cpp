#include "AntiAFK.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "SDK/Game.hpp"
#include "SDK/Offsets.hpp"

#include <imgui.h>
#include <windows.h>

namespace mc {

AntiAFK::AntiAFK()
    : Module("AntiAFK", "Prevents being kicked for inactivity", Category::Misc, 0)
{
    markHasSettings();
}

void AntiAFK::onEnable()
{
    lastAction_ = GetTickCount64();
}

void AntiAFK::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    ULONGLONG now = GetTickCount64();
    if (now - lastAction_ < (ULONGLONG)(intervalSec_ * 1000.0f))
        return;
    lastAction_ = now;

    LocalPlayer local = game.localPlayer();

    if (rotate_)
        local.setYaw(wrapDegrees(local.yaw() + angle_));

    if (jump_ && local.onGround())
    {
        Vec3 vel = local.velocity();
        vel.y = off::gameconst::JumpVelocity;
        local.setVelocity(vel);
    }
}

void AntiAFK::drawSettings()
{
    widgets::CSlider("Interval##afk", &intervalSec_, 5.0f, 60.0f, "%.0f s");
    ImGui::Checkbox("Rotate##afk", &rotate_);
    ImGui::Checkbox("Jump##afk", &jump_);
    widgets::CSlider("Angle##afk", &angle_, 5.0f, 90.0f, "%.0f");
}

MC_REGISTER_MODULE(AntiAFK);

}
