#pragma once

#include "Modules/Module.hpp"

#include <chrono>
#include <cstdint>
#include <unordered_map>

namespace mc {

class Actor;

class KillAura : public Module
{
public:
    KillAura();

    void onEnable() override;
    void onTick() override;
    void onFrame(float dt) override;
    void onDisable() override;
    void drawSettings() override;

    std::string suffix() const override;
    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    bool selectTarget(const Actor& local, Actor& target, float& yaw, float& pitch) const;
    void clearTarget();

    float range_ = 4.0f;
    float fov_ = 180.0f;
    float cps_ = 8.0f;
    float dynamicRangeValue_ = 3.0f;
    float apsMin_ = 6.0f;
    float apsMax_ = 10.0f;
    float nextAttackDelayMs_ = 125.0f;
    float swingDelaySeconds_ = 0.25f;
    int targetMode_ = 0; // Single, Multi, Switch
    int rotateMode_ = 1; // None, Normal, Flick
    bool headYaw_ = false;
    bool dynamicRange_ = false;
    bool randomizeAps_ = false;
    bool swing_ = true;
    bool swingDelay_ = false;
    bool attackThroughWalls_ = true;
    bool autoDisableOnDimensionChange_ = true;
    bool debug_ = false;
    uint64_t multiTargetCursor_ = 0;
    uintptr_t lastDimension_ = 0;
    std::chrono::steady_clock::time_point lastSwing_{};
    std::unordered_map<uintptr_t, std::chrono::steady_clock::time_point>
        lastTargetAttacks_;
    std::chrono::steady_clock::time_point lastAttack_{};
};

}
