#pragma once

#include "Modules/Module.hpp"

namespace mc {

class InventoryWalk : public Module
{
public:
    InventoryWalk();

    void onTick() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"speedMult", speedMult_},
            {"jumpWithSpace", jumpWithSpace_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        speedMult_ = j.value("speedMult", speedMult_);
        jumpWithSpace_ = j.value("jumpWithSpace", jumpWithSpace_);
    }

private:
    float speedMult_ = 1.0f;
    bool jumpWithSpace_ = true;
};

}
