#pragma once

#include "Modules/Module.hpp"

namespace mc {

class LocalPlayer;

class Scaffold : public Module
{
public:
    enum class Strategy
    {
        Auto,
        GameModeWrapper,
        UseItemOnVt,
        BlockItemUseOn,
        LookDownInput,
        PhysicalLook,
    };

    Scaffold();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void drawSettings() override;

private:
    void startPhys(LocalPlayer& local);
    void advancePhys(LocalPlayer& local);

    Strategy strategy_ = Strategy::Auto;
    bool tower_ = true;
    bool platform_ = false;
    bool autoSwap_ = true;
    float retryMs_ = 200.0f;
    int platformY_ = 0;
    bool platformYSolid_ = false;
    unsigned int lastPlaceTick_ = 0;
    float bridgeAngle_ = 60.0f;
    int physPhase_ = 0;
    int physTimer_ = 0;
    int physCooldown_ = 0;
    float physSavedPitch_ = 0.0f;
};

}
