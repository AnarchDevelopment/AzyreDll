#pragma once

#include "Modules/Module.hpp"

namespace mc {

class AntiAFK : public Module
{
public:
    AntiAFK();

    void onEnable() override;
    void onTick() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"intervalSec", intervalSec_},
            {"rotate", rotate_},
            {"jump", jump_},
            {"angle", angle_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        intervalSec_ = j.value("intervalSec", intervalSec_);
        rotate_ = j.value("rotate", rotate_);
        jump_ = j.value("jump", jump_);
        angle_ = j.value("angle", angle_);
    }

private:
    float intervalSec_ = 12.0f;
    bool rotate_ = true;
    bool jump_ = true;
    float angle_ = 25.0f;
    unsigned long long lastAction_ = 0;
};

}
