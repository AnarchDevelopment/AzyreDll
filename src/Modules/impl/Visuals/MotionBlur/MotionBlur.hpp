#pragma once

#include "Modules/Module.hpp"

namespace mc {

// MotionBlur — port of arst-motion-blur (https://github.com/arstnei0/arst-motion-blur).
// Applies full-resolution EMA accumulation blur directly to the game backbuffer
// before ImGui draws, ensuring full clarity (no pixelation) and zero interference
// with game/HUD widgets.
class MotionBlur : public Module
{
public:
    MotionBlur();

    void onEnable() override;
    void onDisable() override;
    void onShutdown() override;
    void onRender() override;
    void drawSettings() override;

    std::string suffix() const override
    {
        return std::to_string((int)strength_) + "%";
    }

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    float strength_ = 75.0f; // 1%..99% (exact arst-motion-blur logic: BlendFactor = strength / 100)
};

}
