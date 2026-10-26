#pragma once

#include "Modules/Module.hpp"

#include <cstdint>

namespace mc {

class Game;
class LocalPlayer;
class Actor;

class TargetHUD : public Module
{
public:
    TargetHUD();
    ~TargetHUD() override;

    void onRender() override;
    void onShutdown() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"fontSize", fontSize_},
            {"scale", scale_},
            {"showHealth", showHealth_},
            {"showStatus", showStatus_},
            {"showDistance", showDistance_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        fontSize_ = j.value("fontSize", fontSize_);
        scale_ = j.value("scale", scale_);
        showHealth_ = j.value("showHealth", showHealth_);
        showStatus_ = j.value("showStatus", showStatus_);
        showDistance_ = j.value("showDistance", showDistance_);
    }

    uintptr_t targetAddress() const { return targetAddr_; }

private:
    void findTarget(const LocalPlayer& local);
    void drawCard(const LocalPlayer& local, const Actor& actor, float dt);
    void updateSkinTexture(const Actor& actor);
    void releaseSkinTexture();

    // Solstice-style card settings.
    float fontSize_ = 20.0f;
    float scale_ = 1.0f;
    bool showHealth_ = true;
    bool showStatus_ = true;
    bool showDistance_ = true;

    // Target comes from KillAura, matching Solstice's Aura::sTarget binding.
    uintptr_t targetAddr_ = 0;
    uintptr_t displayedTarget_ = 0;

    float anim_ = 0.0f;
    float health_ = 20.0f;
    float maxHealth_ = 20.0f;
    float lerpedHealth_ = 20.0f;
    std::string targetName_;
    void* skinSrv_ = nullptr;
    uintptr_t skinOwner_ = 0;
    uint64_t skinHash_ = 0;
};

}
