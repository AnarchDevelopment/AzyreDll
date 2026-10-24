#pragma once

#include "Modules/Module.hpp"

namespace mc {

class UnlockFPS : public Module
{
public:
    UnlockFPS();

    void onEnable() override;
    void onDisable() override;
    void onShutdown() override;
    void drawSettings() override;

    std::string suffix() const override { return "DXGI"; }

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"notify", notify_},
            {"allowTearing", tearing_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        notify_ = j.value("notify", notify_);
        tearing_ = j.value("allowTearing", tearing_);
    }

private:
    bool notify_ = true;
    bool tearing_ = true;
};

}
