#pragma once

#include "Modules/Module.hpp"

#include <Windows.h>

namespace mc {

class AutoGapple final : public Module
{
public:
    AutoGapple();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void drawSettings() override;
    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    int findAppleSlot() const;
    bool sendEquipment(int hotbarSlot);
    void restoreServerSlot();
    void clearOverride();

    float healthThreshold_ = 10.0f;
    bool useEnchantedApples_ = true;
    const char* status_ = "Waiting for local player";
    int overrideSlot_ = -1;
    int originalUserSlot_ = -1;
    ULONGLONG overrideStarted_ = 0;
    ULONGLONG nextAttempt_ = 0;
    bool sawUseAnimation_ = false;
};

} // namespace mc
