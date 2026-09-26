#pragma once

#include "Modules/Module.hpp"

namespace mc {

class LongJump : public Module
{
public:
    LongJump();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"multiplier", multiplier_},
            {"onlySprint", onlySprint_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        multiplier_ = j.value("multiplier", multiplier_);
        onlySprint_ = j.value("onlySprint", onlySprint_);
    }

private:
    float multiplier_ = 1.6f;
    bool onlySprint_ = false;
    bool wasOnGround_ = true;
};

}
