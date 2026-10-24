#pragma once

#include "Modules/Module.hpp"

namespace mc {

class FastPlace : public Module
{
public:
    FastPlace();

    void onEnable() override;
    void onTick() override;
    void drawSettings() override;

    std::string suffix() const override
    {
        return std::to_string(delay_) + "t";
    }

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"delay", delay_},
            {"reach", reach_},
            {"blocksOnly", blocksOnly_},
            {"projectiles", projectiles_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        delay_ = j.value("delay", delay_);
        if (delay_ < 0)
            delay_ = 0;
        if (delay_ > 4)
            delay_ = 4;
        reach_ = j.value("reach", reach_);
        if (reach_ < 3.0f)
            reach_ = 3.0f;
        if (reach_ > 7.0f)
            reach_ = 7.0f;
        blocksOnly_ = j.value("blocksOnly", blocksOnly_);
        projectiles_ = j.value("projectiles", projectiles_);
    }

private:
    int delay_ = 0;
    float reach_ = 5.25f;
    bool blocksOnly_ = true;
    bool projectiles_ = false;
    int tickCount_ = 0;
};

}
