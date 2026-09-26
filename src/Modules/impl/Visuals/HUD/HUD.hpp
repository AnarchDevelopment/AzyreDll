#pragma once

#include "Modules/Module.hpp"

namespace mc {

class HUD : public Module
{
public:
    HUD();

    void onRender() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"stats", stats_},
            {"speed", speed_},
            {"direction", direction_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        stats_ = j.value("stats", stats_);
        speed_ = j.value("speed", speed_);
        direction_ = j.value("direction", direction_);
    }

private:
    bool stats_ = true;
    bool speed_ = true;
    bool direction_ = true;
};

}
