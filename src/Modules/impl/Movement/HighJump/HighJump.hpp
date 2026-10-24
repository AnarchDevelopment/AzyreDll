#pragma once

#include "Modules/Module.hpp"

namespace mc {

class HighJump : public Module
{
public:
    HighJump();

    void onTick() override;
    void drawSettings() override;

    std::string suffix() const override
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.2f", boost_);
        return buf;
    }

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"boost", boost_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        boost_ = j.value("boost", boost_);
    }

private:
    float boost_ = 0.60f;
};

}
