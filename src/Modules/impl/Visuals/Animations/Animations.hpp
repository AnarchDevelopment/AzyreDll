#pragma once

#include "Modules/Module.hpp"

namespace mc {

class Animations : public Module
{
public:
    Animations();

    void onEnable() override;
    void onDisable() override;
    void onShutdown() override;
    void drawSettings() override;

    std::string suffix() const override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    // Swing Time is gated by a checkbox so the vanilla cadence is never retimed
    // by accident: the slider only exists while the toggle is on.
    bool swingTimeEnabled_ = true;
    int swingTime_ = 6;
    bool customSwingAngle_ = true;
    bool fluxSwing_ = false;
    bool onlyOnBlockSwingAngle_ = false;
    float swingAngle_ = -31.0f;
    bool smallItems_ = true;
    float smallItemsScale_ = 1.0f; // Fixed: was 0.75f, gain must be 1.0 (not 1/0.4) to match Rise's ratio
    bool swordBlock_ = false;
    bool onlyOnBlock_ = true;
    // Index into the Rise block animation preset table (0 = Custom, the three
    // rotation sliders below).
    int blockPreset_ = 0;
    float blockXRotation_ = -57.0f;
    float blockYRotation_ = -45.16f;
    float blockZRotation_ = 13.14f;
};

}
