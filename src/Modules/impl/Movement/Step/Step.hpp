#pragma once

#include "Modules/Module.hpp"

#include <string>

namespace mc {

class LocalPlayer;
class BlockSource;

// Auto Step: climbs 1-2 block walls without jumping.
//
//  - Motion: writes an upward velocity while the feet are blocked against
//    the step; the game's own physics sweep resolves the climb (smooth,
//    same trick Flight/Speed use — nothing but float writes).
//  - Clip: instant step-up following the proven Scaffold "Tower Clip"
//    write pattern (mPosition + translated AABB + network move target
//    +0xD08). The obstacle and the air clearance are verified with real
//    BlockSource reads BEFORE any write, so the player is never teleported
//    into a solid block.
//
// Crash safety (the reason this module is wall-to-wall guarded): every
// read is a guarded mem read on a validated LocalPlayer, every block
// query goes through BlockSource::getBlockId's SEH wrapper, and the only
// positional write mirrors the tested clipLocalPlayerUp layout exactly
// (isReadable check + rawRead/rawWrite of the AABB). Any fault the guard
// misses is contained by the ModuleManager SEH (module disabled, never a
// process crash).
class Step : public Module
{
public:
    enum Mode
    {
        kMotion = 0,
        kClip = 1,
    };

    Step();

    void onEnable() override;
    void onTick() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

    std::string suffix() const override
    {
        return mode_ == kMotion ? "Motion" : "Clip";
    }

private:
    void tickStep(LocalPlayer& local, const BlockSource& bs);
    void displayDebug(const std::string& msg);

    int mode_ = kMotion;
    int heightBlocks_ = 1;   // 1 or 2 blocks
    float liftSpeed_ = 0.35f; // Motion mode: vy written per tick
    float blockedSpeed_ = 0.08f; // vxz under this while pressing move keys = wall
    bool debug_ = false;

    // State
    ULONGLONG lastStepMs_ = 0;
    float stepTargetY_ = 0.0f; // Motion mode: feet Y we are climbing to
    bool stepping_ = false;
};

}
