#pragma once

#include "Modules/Module.hpp"

namespace mc {

class TargetHUD : public Module
{
public:
    TargetHUD();

    void onRender() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"range", range_},
            {"fov", fov_},
            {"showHealth", showHealth_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        range_ = j.value("range", range_);
        fov_ = j.value("fov", fov_);
        showHealth_ = j.value("showHealth", showHealth_);
    }

private:
    float range_ = 8.0f;
    float fov_ = 30.0f;
    bool showHealth_ = true;
};

}
