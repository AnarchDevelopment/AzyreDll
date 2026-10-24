#pragma once

#include "Modules/Module.hpp"

#include <windows.h>

namespace mc {

class AutoClicker : public Module
{
public:
    AutoClicker();

    void onTick() override;
    void drawSettings() override;

    std::string suffix() const override
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%dcps", cps_);
        return buf;
    }

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"cps", cps_},
            {"left", left_},
            {"right", right_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        cps_ = j.value("cps", cps_);
        left_ = j.value("left", left_);
        right_ = j.value("right", right_);
    }

private:
    int cps_ = 12;
    bool left_ = true;
    bool right_ = false;
    ULONGLONG lastClick_ = 0;
};

}