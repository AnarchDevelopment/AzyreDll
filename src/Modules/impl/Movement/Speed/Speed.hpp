#pragma once

#include "Modules/Module.hpp"

namespace mc {

class LocalPlayer;

// Puerto del Speed de Solstice: modos Friction/Legit, randomize, strafe,
// fast fall (x2), tipos de salto y damage boost.
class Speed : public Module
{
public:
    enum Mode
    {
        kFriction = 0,
        kLegit = 1,
    };
    enum FastFallMode
    {
        kFFNone = 0,
        kFFPredict,
        kFFSetVel,
    };
    enum JumpType
    {
        kJumpVanilla = 0,
        kJumpVelocity,
        kJumpNone,
    };

    Speed();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    void tickFriction(LocalPlayer& local);
    void tickLegit(LocalPlayer& local);
    void applyFastFall(LocalPlayer& local);
    void handleJump(LocalPlayer& local);

    int mode_ = kFriction;

    float speed_ = 0.5f;
    bool randomize_ = false;
    float minSpeed_ = 0.5f;
    float maxSpeed_ = 1.0f;

    bool strafe_ = true;           // mStrafe: permite strafe en getMotion
    bool useStrafeSpeed_ = true;   // mUseStrafeSpeed
    float strafeSpeed_ = 0.5f;
    bool dontBoostStrafe_ = true;  // mDontBoosStrafeSpeed ("Ignore Strafe")
    float friction_ = 0.975f;

    int fastFall_ = kFFNone;
    int fallTicks_ = 5;
    float fallSpeed_ = 1.0f;
    bool fastFall2_ = false;
    int fallTicks2_ = 5;
    float fallSpeed2_ = 1.0f;

    int jumpType_ = kJumpVanilla;
    float jumpHeight_ = 0.42f;

    bool damageBoost_ = false;
    float damageBoostSpeed_ = 3.0f;
    float damageBoostSlowdown_ = 0.15f;

    // Estado por tick
    float frictionState_ = 1.0f;
    int airTick_ = 0;
    bool lastOnGround_ = true;
    bool wasHurt_ = false;
    float damageBoostVal_ = 1.0f;
};

}
