#pragma once

#include "Modules/Module.hpp"

namespace mc {

class MyNewModule : public Module
{
public:
    MyNewModule();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void onRender() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"exampleValue", exampleValue_},
            {"exampleFlag", exampleFlag_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        exampleValue_ = j.value("exampleValue", exampleValue_);
        exampleFlag_ = j.value("exampleFlag", exampleFlag_);
    }

private:
    float exampleValue_ = 1.0f;
    bool exampleFlag_ = false;
};

}
