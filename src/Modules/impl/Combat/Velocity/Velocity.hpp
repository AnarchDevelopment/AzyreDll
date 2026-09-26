#pragma once

#include "Modules/Module.hpp"

namespace mc {

class Velocity : public Module
{
public:
    Velocity();

    void onEnable() override;
    void onDisable() override;
    void onShutdown() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"chance", chance_},
            {"hPercent", hPercent_},
            {"vPercent", vPercent_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        chance_ = j.value("chance", chance_);
        hPercent_ = j.value("hPercent", hPercent_);
        vPercent_ = j.value("vPercent", vPercent_);
    }

private:
    int chance_ = 100;
    int hPercent_ = 100;
    int vPercent_ = 100;
};

}
