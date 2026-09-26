#pragma once

#include "Modules/Module.hpp"

namespace mc {

class NoSlow : public Module
{
public:
    NoSlow();

    void onTick() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"restoreSpeed", restoreSpeed_},
            {"onlyGround", onlyGround_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        restoreSpeed_ = j.value("restoreSpeed", restoreSpeed_);
        onlyGround_ = j.value("onlyGround", onlyGround_);
    }

private:
    float restoreSpeed_ = 0.216f;
    bool onlyGround_ = true;
};

}
