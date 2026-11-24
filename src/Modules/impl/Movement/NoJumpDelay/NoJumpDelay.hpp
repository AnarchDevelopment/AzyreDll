#pragma once

#include "Modules/Module.hpp"

namespace mc {

class NoJumpDelay : public Module
{
public:
    NoJumpDelay();

    void onEnable() override;
    void onTick() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    float jumpHeight_ = 0.42f;
    float boost_ = 0.20f;
    bool preserveSpeed_ = true;
    bool requireSpace_ = true;
    bool onlyMoving_ = false;

    float prevAirSpeed_ = 0.0f;
};

} // namespace mc
