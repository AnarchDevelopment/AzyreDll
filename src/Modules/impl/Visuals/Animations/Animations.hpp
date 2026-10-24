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
    int swingTime_ = 6;
    bool customSwingAngle_ = true;
    bool onlyOnBlockSwingAngle_ = false;
    float swingAngle_ = -31.0f;
    bool smallItems_ = true;
    bool thirdPersonBlock_ = false;
    bool onlyOnBlock_ = true;
    float blockXRotation_ = -57.0f;
    float blockYRotation_ = -45.16f;
    float blockZRotation_ = 13.14f;
};

}
