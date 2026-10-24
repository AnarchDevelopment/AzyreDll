#pragma once

#include "Modules/Module.hpp"

#include <string>

namespace mc {

class LocalPlayer;

// Port of Solstice's Fly. Modes:
//  - Motion: classic velocity fly (yaw motion at speed/10, Space/Shift up
//    and down, idle = full stop hover). Azyre extras: anti-kick hover.
//  - Pregame: fixed 0.255 speed, -0.0005 hover when idle (pre-game lobbies).
//  - Jump: airjump fly — friction, height loss per jump, jump delay, and
//    optional "damage only" window (isHurt based instead of the incoming
//    SetActorMotionPacket, which Azyre cannot hook yet).
// Not portable yet: Elytra (needs PlayerAuthInputPacket::mInputData offsets)
// and Vertical (needs the AABB component offsets).
class Flight : public Module
{
public:
    enum Mode
    {
        kMotion = 0,
        kPregame = 1,
        kJump = 2,
    };

    Flight();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

    std::string suffix() const override
    {
        switch (mode_)
        {
        case kMotion: return "Motion";
        case kPregame: return "Pregame";
        case kJump: return "Jump";
        default: return "";
        }
    }

private:
    bool tickJump(LocalPlayer& local, float forward, float right);
    void jump(LocalPlayer& local, float forward, float right);
    void displayDebug(const std::string& msg);

    int mode_ = kMotion;
    float speed_ = 5.6f; // 0..20, applied as speed/10 like Solstice

    // Motion extras (Azyre)
    bool antiKick_ = true;
    float kickInterval_ = 40.0f;

    // Jump mode (Solstice settings)
    bool speedFriction_ = true;
    float friction_ = 0.975f;
    float heightLoss_ = 0.5f;
    float jumpDelay_ = 0.2f;
    bool damageOnly_ = false;
    float flyTime_ = 1.0f;
    bool resetOnGround_ = true;
    bool resetOnDisable_ = true;
    bool debug_ = false;

    // State
    float currentY_ = 0.0f;
    float currentFriction_ = 1.0f;
    ULONGLONG lastJump_ = 0;
    ULONGLONG lastDamage_ = 0;
    bool wasHurt_ = false;
    bool flying_ = false;
    int airTicks_ = 0;
};

}
