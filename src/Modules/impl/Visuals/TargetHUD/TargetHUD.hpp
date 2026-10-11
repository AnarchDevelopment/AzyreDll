#pragma once

#include "Modules/Module.hpp"

namespace mc {

class Game;
class LocalPlayer;
class Actor;

class TargetHUD : public Module
{
public:
    TargetHUD();

    void onRender() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"range", range_},
            {"fov", fov_},
            {"showHealth", showHealth_},
            {"showDistance", showDistance_},
            {"scale", scale_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        range_ = j.value("range", range_);
        fov_ = j.value("fov", fov_);
        showHealth_ = j.value("showHealth", showHealth_);
        showDistance_ = j.value("showDistance", showDistance_);
        scale_ = j.value("scale", scale_);
    }

    uintptr_t targetAddress() const { return targetAddr_; }

private:
    void findTarget(Game& game, const LocalPlayer& local);
    void drawCard(const LocalPlayer& local, const Actor& actor, float dt);

    // Settings
    float range_ = 8.0f;
    float fov_ = 30.0f;
    bool showHealth_ = true;
    bool showDistance_ = true;
    float scale_ = 1.0f;

    // Target tracking: dirección directa (real-time)
    uintptr_t targetAddr_ = 0;
    ULONGLONG lastSeen_ = 0;

    // Animacion
    float anim_ = 0.0f;
    float lerpedHealth_ = 20.0f;
    std::string targetName_;
};

}
