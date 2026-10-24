#pragma once

#include "Framework/Math.hpp"
#include "Modules/Module.hpp"

#include <windows.h>
#include <string>

namespace mc {

class LocalPlayer;
class BlockSource;

class Phase : public Module
{
public:
    enum Mode
    {
        kAlways = 0,
        kSneakOnly = 1,
    };

    Phase();

    void onEnable() override;
    void onTick() override;
    void drawSettings() override;

    std::string suffix() const override
    {
        return mode_ == kAlways ? "Always" : "Sneak";
    }

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"mode", mode_},
            {"distance", distance_},
            {"cooldown", cooldownMs_},
            {"debug", debug_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        mode_ = j.value("mode", mode_);
        if (mode_ != kAlways && mode_ != kSneakOnly)
            mode_ = kAlways;
        distance_ = clampf(j.value("distance", distance_), 1.0f, 4.0f);
        cooldownMs_ = j.value("cooldown", cooldownMs_);
        if (cooldownMs_ < 50) cooldownMs_ = 50;
        if (cooldownMs_ > 500) cooldownMs_ = 500;
        debug_ = j.value("debug", debug_);
    }

private:
    void tickPhase(LocalPlayer& local, const BlockSource& bs);
    void displayDebug(const std::string& msg);

    int mode_ = kAlways;
    float distance_ = 2.6f;
    int cooldownMs_ = 150;
    bool debug_ = false;

    ULONGLONG lastPhaseMs_ = 0;
};

}
