#pragma once

#include "Modules/Module.hpp"

namespace mc {

// MotionBlur — port of Solstice's MotionBlur. The blur helper keeps a ring
// of ghost frames (quarter-res captures of the last kGhostMax frames);
// this module blends them over the screen with an exponential intensity
// falloff. The smear appears naturally while moving: static scenes blend
// into themselves and stay sharp.
class MotionBlur : public Module
{
public:
    MotionBlur();

    void onEnable() override;
    void onDisable() override;
    void onShutdown() override;
    void onRender() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    int maxFrames_ = 1;      // ghost frames to blend (1..20)
    float intensity_ = 0.9f; // alpha of the most recent ghost (0.01..1)
};

}
