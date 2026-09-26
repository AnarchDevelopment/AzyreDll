#pragma once

#include "Framework/Math.hpp"
#include "Modules/Module.hpp"

namespace mc {

class AntiVoid : public Module
{
public:
    AntiVoid();

    void onEnable() override;
    void onTick() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"distance", distance_},
            {"rescueVel", rescueVel_},
            {"cooldown", cooldown_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        distance_ = j.value("distance", distance_);
        rescueVel_ = j.value("rescueVel", rescueVel_);
        cooldown_ = j.value("cooldown", cooldown_);
    }

private:
    int distance_ = 10;
    float rescueVel_ = 0.6f;
    int cooldown_ = 500;
    unsigned long long nextRescue_ = 0;
    unsigned int rescues_ = 0;
    Vec3 safePos_{};
    bool hasSafe_ = false;
};

}
