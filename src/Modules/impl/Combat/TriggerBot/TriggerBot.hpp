#pragma once

#include "Modules/Module.hpp"

namespace mc {

class TriggerBot : public Module
{
public:
    TriggerBot();

    void onEnable() override;
    void onFrame(float dt) override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"range", range_},
            {"fov", fov_},
            {"delay", delay_},
            {"chance", chance_},
            {"method", method_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        range_ = j.value("range", range_);
        fov_ = j.value("fov", fov_);
        delay_ = j.value("delay", delay_);
        chance_ = j.value("chance", chance_);
        method_ = j.value("method", method_);
    }

private:
    float range_ = 4.0f;
    float fov_ = 10.0f;
    int delay_ = 100;
    int chance_ = 100;
    int method_ = 0;
    unsigned long long nextClick_ = 0;
    unsigned int clicks_ = 0;
};

}
