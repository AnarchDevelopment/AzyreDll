#pragma once

#include "Modules/Module.hpp"

namespace mc {

class Speed : public Module
{
public:
    Speed();

    void onEnable() override;
    void onTick() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"speedPerTick", speedPerTick_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        speedPerTick_ = j.value("speedPerTick", speedPerTick_);
    }

private:
    float speedPerTick_ = 0.35f;
};

}
