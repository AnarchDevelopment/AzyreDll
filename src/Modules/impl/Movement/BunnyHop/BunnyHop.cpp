#include "BunnyHop.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

namespace mc {

static bool keyHeld(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static void computeRestore(float savedX, float savedZ, float preserve, float maxAbs,
                           float& outX, float& outZ)
{
    outX = savedX * preserve;
    outZ = savedZ * preserve;
    float h = sqrtf(outX * outX + outZ * outZ);
    if (h > maxAbs)
    {
        float s = maxAbs / h;
        outX *= s;
        outZ *= s;
    }
}

BunnyHop::BunnyHop()
    : Module("BunnyHop", "Bhop + air strafe (formula Quake/Source)", Category::Movement, 0)
{
    markHasSettings();
}

void BunnyHop::resetState()
{
    lastOnGround_ = 1;
    airTicks_ = 0;
    tickCount_ = 0;
    lastJumpTick_ = -100;
    savedAirVx_ = 0.0f;
    savedAirVz_ = 0.0f;
    savedAirSpeed_ = 0.0f;
}

void BunnyHop::onEnable()
{
    resetState();
}

void BunnyHop::onDisable()
{
    resetState();
}

void BunnyHop::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    ++tickCount_;

    Vec3 vel = local.velocity();
    float vx = vel.x;
    float vy = vel.y;
    float vz = vel.z;
    float yaw = local.yaw();
    int onGround = local.onGround() ? 1 : 0;
    float hSpeed = vecLength2D(vel);

    if (onGround == 0 && lastOnGround_ == 1)
        airTicks_ = 0;
    else if (onGround == 0)
        airTicks_++;
    else
        airTicks_ = 0;

    if (onGround == 0 && hSpeed > 0.05f)
    {
        savedAirVx_ = vx;
        savedAirVz_ = vz;
        savedAirSpeed_ = hSpeed;
    }

    bool justLanded = (onGround == 1 && lastOnGround_ == 0);
    if (restoreOnLand_ && justLanded && savedAirSpeed_ > 0.05f)
    {
        float rx = 0.0f, rz = 0.0f;
        computeRestore(savedAirVx_, savedAirVz_, landingPreserve_, vMaxAirAbs_, rx, rz);
        local.setVelocityXZ({rx, 0.0f, rz});
        vx = rx;
        vz = rz;
        hSpeed = sqrtf(vx * vx + vz * vz);
    }

    if (autoJump_ && onGround == 1)
    {
        int sinceJump = tickCount_ - lastJumpTick_;
        if (sinceJump >= jumpCooldown_)
        {
            bool isMoving = hSpeed > 0.05f || keyHeld('W') || keyHeld('A') ||
                            keyHeld('S') || keyHeld('D');
            if (isMoving)
            {
                Vec3 jump = local.velocity();
                jump.y = velocity_;

                if (restoreOnLand_ && savedAirSpeed_ > 0.05f)
                {
                    float rx = 0.0f, rz = 0.0f;
                    computeRestore(savedAirVx_, savedAirVz_, landingPreserve_, vMaxAirAbs_, rx, rz);
                    jump.x = rx;
                    jump.z = rz;
                    vx = rx;
                    vz = rz;
                }

                local.setVelocity(jump);
                vy = velocity_;
                lastJumpTick_ = tickCount_;
            }
        }
    }

    if (airStrafe_ && onGround == 0 && airTicks_ >= minAirTicks_)
    {
        float yRad = deg2rad(yaw);
        float fwdX = -std::sin(yRad);
        float fwdZ = std::cos(yRad);
        float rightX = -fwdZ;
        float rightZ = fwdX;

        float wishX = 0.0f;
        float wishZ = 0.0f;
        if (keyHeld('W')) { wishX += fwdX; wishZ += fwdZ; }
        if (keyHeld('S')) { wishX -= fwdX; wishZ -= fwdZ; }
        if (keyHeld('D')) { wishX += rightX; wishZ += rightZ; }
        if (keyHeld('A')) { wishX -= rightX; wishZ -= rightZ; }

        float wishLen = sqrtf(wishX * wishX + wishZ * wishZ);
        if (wishLen > 0.001f)
        {
            wishX /= wishLen;
            wishZ /= wishLen;

            float vProj = vx * wishX + vz * wishZ;
            float dvLim = vMaxAir_ - vProj;
            if (dvLim < 0.0f)
                dvLim = 0.0f;
            float dvTeorica = airAccel_ * vMaxAir_ * 0.05f;
            float dv = dvTeorica < dvLim ? dvTeorica : dvLim;

            vx += dv * wishX;
            vz += dv * wishZ;

            float newH = sqrtf(vx * vx + vz * vz);
            if (newH > vMaxAirAbs_)
            {
                float s = vMaxAirAbs_ / newH;
                vx *= s;
                vz *= s;
            }

            local.setVelocityXZ({vx, 0.0f, vz});
        }
    }

    lastOnGround_ = onGround;
}

void BunnyHop::drawSettings()
{
    ImGui::Checkbox("Auto jump##bh", &autoJump_);
    ImGui::Checkbox("Air strafe##bh", &airStrafe_);
    widgets::CSlider("Jump velocity##bh", &velocity_, 0.20f, 0.80f, "%.2f");
    widgets::CSlider("Jump cooldown##bh", &jumpCooldown_, 1, 10);
    widgets::CSlider("Air accel (a)##bh", &airAccel_, 0.01f, 0.50f, "%.3f");
    widgets::CSlider("V max air##bh", &vMaxAir_, 0.20f, 2.00f, "%.2f");
    widgets::CSlider("V max abs##bh", &vMaxAirAbs_, 0.50f, 5.00f, "%.2f");
    widgets::CSlider("Min air ticks##bh", &minAirTicks_, 1, 5);
    widgets::CSlider("Landing preserve##bh", &landingPreserve_, 0.50f, 1.00f, "%.2f");
    ImGui::Checkbox("Restore on land##bh", &restoreOnLand_);
}

MC_REGISTER_MODULE(BunnyHop);

}
