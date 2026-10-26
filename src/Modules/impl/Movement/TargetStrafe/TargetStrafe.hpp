#pragma once

#include "Modules/Module.hpp"

#include <cstdint>

namespace mc {

class TargetStrafe : public Module
{
public:
    TargetStrafe();

    void onEnable() override;
    void onFrame(float dt) override;
    void onRender() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;
    std::string suffix() const override;

private:
    float range_ = 2.4f;
    float thickness_ = 1.5f;
    bool holdJump_ = true;
    bool circle_ = true;
    bool clockwise_ = true;
    uintptr_t lastTarget_ = 0;
};

}
