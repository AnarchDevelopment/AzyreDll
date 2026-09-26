#pragma once

#include "Modules/Module.hpp"

namespace mc {

class Friends : public Module
{
public:
    Friends();

    void onTick() override;
    void onDisable() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    float range_ = 6.0f;
    float fov_ = 30.0f;
    bool prevMiddle_ = false;
};

}
