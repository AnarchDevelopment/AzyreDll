#pragma once

#include "Modules/Module.hpp"

namespace mc {

class BunnyHop : public Module
{
public:
    BunnyHop();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void drawSettings() override;

private:
    void resetState();

    bool autoJump_ = true;
    bool airStrafe_ = true;
    float velocity_ = 0.42f;
    int jumpCooldown_ = 3;
    float airAccel_ = 0.10f;
    float vMaxAir_ = 0.60f;
    float vMaxAirAbs_ = 2.00f;
    int minAirTicks_ = 2;
    float landingPreserve_ = 0.95f;
    bool restoreOnLand_ = true;

    int lastOnGround_ = 1;
    int airTicks_ = 0;
    int tickCount_ = 0;
    int lastJumpTick_ = -100;
    float savedAirVx_ = 0.0f;
    float savedAirVz_ = 0.0f;
    float savedAirSpeed_ = 0.0f;
};

}
