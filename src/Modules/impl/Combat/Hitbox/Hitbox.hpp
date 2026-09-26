#pragma once

#include "Modules/Module.hpp"
#include "SDK/CodePatch.hpp"

namespace mc {

class Hitbox : public Module
{
public:
    Hitbox();

    void onEnable() override;
    void onDisable() override;
    void onShutdown() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"value", value_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        value_ = j.value("value", value_);
    }

private:
    float value_ = 0.6f;
    patch::CodePatch patch_;
};

}
