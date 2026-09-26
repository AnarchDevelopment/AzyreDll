#pragma once

#include "Modules/Module.hpp"

namespace mc {

class AimAssist : public Module
{
public:
    AimAssist();

    void onFrame(float dt) override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"range", range_},
            {"fov", fov_},
            {"speedH", speedH_},
            {"speedV", speedV_},
            {"onlyWeapons", onlyWeapons_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        range_ = j.value("range", range_);
        fov_ = j.value("fov", fov_);
        speedH_ = j.value("speedH", speedH_);
        speedV_ = j.value("speedV", speedV_);
        onlyWeapons_ = j.value("onlyWeapons", onlyWeapons_);
    }

private:
    float range_ = 4.0f;
    float fov_ = 90.0f;
    float speedH_ = 0.35f;
    float speedV_ = 0.25f;
    bool onlyWeapons_ = false;
};

}
