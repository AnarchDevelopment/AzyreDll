#pragma once

#include "Modules/Module.hpp"

namespace mc {

class Criticals : public Module
{
public:
    Criticals();

    void onTick() override;
    void onDisable() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"onlyWhileAttacking", onlyWhileAttacking_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        onlyWhileAttacking_ = j.value("onlyWhileAttacking", onlyWhileAttacking_);
    }

private:
    bool onlyWhileAttacking_ = true;
    int phase_ = 0;
    bool wasAttacking_ = false;
};

}
