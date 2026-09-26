#pragma once

#include "Modules/Module.hpp"

namespace mc {

class NoFall : public Module
{
public:
    NoFall();

    void onTick() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"ghostGround", ghostGround_},
            {"ghostThreshold", ghostThreshold_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        ghostGround_ = j.value("ghostGround", ghostGround_);
        ghostThreshold_ = j.value("ghostThreshold", ghostThreshold_);
    }

private:
    bool ghostGround_ = true;
    float ghostThreshold_ = -1.8f;
};

}
