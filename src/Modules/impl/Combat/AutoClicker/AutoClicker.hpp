#pragma once

#include "Modules/Module.hpp"

namespace mc {

class AutoClicker : public Module
{
public:
    AutoClicker();

    void onEnable() override;
    void onDisable() override;
    void onShutdown() override;
    void onFrame(float dt) override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"cps", cps_},
            {"jitter", jitter_},
            {"method", method_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        cps_ = j.value("cps", cps_);
        jitter_ = j.value("jitter", jitter_);
        method_ = j.value("method", method_);
    }

private:
    int cps_ = 12;
    int jitter_ = 15;
    int method_ = 0;
    unsigned long long nextClick_ = 0;
    unsigned int clicks_ = 0;
    bool injected_ = false;
};

}
