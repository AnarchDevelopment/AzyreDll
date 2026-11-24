#include "NoJumpDelay.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "GUI/Menu.hpp"
#include "SDK/Entity/LocalPlayer.hpp"
#include "SDK/Core/Game.hpp"
#include "SDK/Engine/Memory/Offsets.hpp"

#include <windows.h>
#include <cmath>

namespace mc {

namespace {

bool keyDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool isCursorShowing()
{
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (GetCursorInfo(&ci))
        return (ci.flags & CURSOR_SHOWING) != 0;
    return false;
}

} // namespace

NoJumpDelay::NoJumpDelay()
    : Module("NoJumpDelay", "Removes delay between consecutive jumps.", Category::Movement, 0)
{
    markHasSettings();
}

void NoJumpDelay::onEnable()
{
    prevAirSpeed_ = 0.0f;
}

void NoJumpDelay::onTick()
{
    if (menu::visible() || isCursorShowing())
        return;

    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    if (!local.valid())
        return;

    Vec3 vel = local.velocity();
    if (!std::isfinite(vel.x) || !std::isfinite(vel.y) || !std::isfinite(vel.z))
        return;

    float currentSpeed = vecLength2D(vel);

    if (local.onGround())
    {
        if (requireSpace_ && !keyDown(VK_SPACE))
        {
            prevAirSpeed_ = currentSpeed;
            return;
        }

        if (onlyMoving_)
        {
            bool moving = keyDown('W') || keyDown('A') || keyDown('S') || keyDown('D');
            if (!moving)
            {
                prevAirSpeed_ = currentSpeed;
                return;
            }
        }

        // Landing on ground or standing
        if (vel.y <= 0.08f)
        {
            vel.y = jumpHeight_;

            float fwd = 0.0f;
            float str = 0.0f;
            if (keyDown('W')) fwd += 1.0f;
            if (keyDown('S')) fwd -= 1.0f;
            if (keyDown('D')) str += 1.0f;
            if (keyDown('A')) str -= 1.0f;

            bool hasInput = (std::abs(fwd) > 0.01f || std::abs(str) > 0.01f);
            Vec3 dir{};
            if (hasInput)
                dir = wishDirFromYaw(local.yaw(), fwd, str);
            else if (currentSpeed > 0.05f)
                dir = {vel.x / currentSpeed, 0.0f, vel.z / currentSpeed};

            if (vecLength2D(dir) > 0.01f)
            {
                // In vanilla Minecraft, jumping while sprinting adds +0.2 momentum in direction of motion.
                // When landing, ground friction immediately cuts speed unless preserved or boosted.
                if (preserveSpeed_ && prevAirSpeed_ > currentSpeed)
                {
                    float targetSpeed = prevAirSpeed_ + (boost_ * 0.5f);
                    vel.x = dir.x * targetSpeed;
                    vel.z = dir.z * targetSpeed;
                }
                else if (boost_ > 0.0f)
                {
                    vel.x += dir.x * boost_;
                    vel.z += dir.z * boost_;
                }
            }

            local.setVelocity(vel);
            // Do NOT call local.setOnGround(false) - the engine's movement update
            // handles lift-off cleanly and doesn't cancel ground acceleration.
        }

        prevAirSpeed_ = vecLength2D(local.velocity());
    }
    else
    {
        prevAirSpeed_ = currentSpeed;
    }
}

void NoJumpDelay::drawSettings()
{
    widgets::CSlider("Jump Height##njd", &jumpHeight_, 0.20f, 0.80f, "%.2f");
    widgets::CSlider("Jump Boost##njd", &boost_, 0.00f, 0.50f, "%.2f");
    widgets::Checkbox("Preserve Speed##njd", &preserveSpeed_);
    widgets::Checkbox("Require Space##njd", &requireSpace_);
    widgets::Checkbox("Only Moving##njd", &onlyMoving_);
}

nlohmann::json NoJumpDelay::saveSettings() const
{
    return {
        {"jumpHeight", jumpHeight_},
        {"boost", boost_},
        {"preserveSpeed", preserveSpeed_},
        {"requireSpace", requireSpace_},
        {"onlyMoving", onlyMoving_},
    };
}

void NoJumpDelay::loadSettings(const nlohmann::json& j)
{
    jumpHeight_ = clampf(j.value("jumpHeight", jumpHeight_), 0.20f, 0.80f);
    boost_ = clampf(j.value("boost", boost_), 0.00f, 0.50f);
    preserveSpeed_ = j.value("preserveSpeed", preserveSpeed_);
    requireSpace_ = j.value("requireSpace", requireSpace_);
    onlyMoving_ = j.value("onlyMoving", onlyMoving_);
}

MC_REGISTER_MODULE(NoJumpDelay);

} // namespace mc
