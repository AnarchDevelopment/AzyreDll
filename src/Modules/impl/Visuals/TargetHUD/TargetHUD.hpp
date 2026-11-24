#pragma once

#include "Framework/Math.hpp"
#include "Modules/Module.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace mc {

class Game;
class LocalPlayer;
class Actor;

class TargetHUD : public Module
{
public:
    TargetHUD();
    ~TargetHUD() override;

    void onEnable() override;
    void onDisable() override;
    void onRender() override;
    void onShutdown() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"mode", mode_},
            {"riseBackground", riseBackground_},
            {"riseParticles", riseParticles_},
            {"riseFollowPlayer", riseFollowPlayer_},
            {"fontSize", fontSize_},
            {"scale", scale_},
            {"showHealth", showHealth_},
            {"showStatus", showStatus_},
            {"showDistance", showDistance_},
            {"damageMode", damageMode_},
            {"customDamage", customDamage_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        mode_ = j.value("mode", mode_);
        if (mode_ < 0 || mode_ > 1) mode_ = 0;
        riseBackground_ = j.value("riseBackground", riseBackground_);
        if (riseBackground_ < 0 || riseBackground_ > 2) riseBackground_ = 0;
        riseParticles_ = j.value("riseParticles", riseParticles_);
        riseFollowPlayer_ = j.value("riseFollowPlayer", riseFollowPlayer_);
        fontSize_ = j.value("fontSize", fontSize_);
        scale_ = j.value("scale", scale_);
        showHealth_ = j.value("showHealth", showHealth_);
        showStatus_ = j.value("showStatus", showStatus_);
        showDistance_ = j.value("showDistance", showDistance_);
        damageMode_ = j.value("damageMode", damageMode_);
        if (damageMode_ < 0 || damageMode_ > 1) damageMode_ = 0;
        customDamage_ = j.value("customDamage", customDamage_);
        customDamage_ = std::clamp(customDamage_, 0.5f, 20.0f);
    }

    uintptr_t targetAddress() const { return targetAddr_; }

private:
    void findTarget(const LocalPlayer& local);
    void renderRise(const LocalPlayer& local, float dt);
    void updateRiseAnimation(bool showing, uint64_t nowMs);
    void drawRiseCard(const LocalPlayer& local, float dt);
    void drawCard(const LocalPlayer& local, const Actor& actor, float dt);
    bool updateHealth(const Actor& actor, float maxHealth, float dt);
    void updateSkinTexture(const Actor& actor);
    void releaseSkinTexture();

    // TargetHUD style settings.
    int mode_ = 0; // 0 = Azyre, 1 = Rise Modern
    int riseBackground_ = 0; // Glass, Tint, Solid
    bool riseParticles_ = true;
    bool riseFollowPlayer_ = false;
    float fontSize_ = 20.0f;
    float scale_ = 1.0f;
    bool showHealth_ = true;
    bool showStatus_ = true;
    bool showDistance_ = true;

    // Vida del objetivo: el server no la replica en esta build, asi que el HUD
    // descuenta el dano de cada golpe enviado por GameMode::attack.
    // 0 = arma en mano (tabla por id legacy), 1 = valor fijo por impacto.
    int damageMode_ = 0;
    float customDamage_ = 4.0f;
    // El server aplica dano como mucho una vez por ventana de inmunidad
    // (hurtTime = 10 ticks en vanilla). Los ataques que llegan dentro de la
    // ventana no restan vida; no se expone como ajuste porque es una constante
    // del juego, no una preferencia.
    static constexpr uint64_t kHurtImmunityMs = 500;
    static constexpr float kRespawnTeleportDistance = 24.0f;

    // Target comes from KillAura, matching Solstice's Aura::sTarget binding.
    uintptr_t targetAddr_ = 0;
    uintptr_t displayedTarget_ = 0;
    uintptr_t riseTargetAddr_ = 0;
    uint64_t riseLastTargetMs_ = 0;
    uint64_t riseAnimStartMs_ = 0;
    float riseAnimFrom_ = 0.0f;
    float riseAnimTarget_ = 0.0f;
    uint32_t riseAnimDurationMs_ = 850;
    float riseDistance_ = 0.0f;
    float riseLocalHealth_ = -1.0f;
    bool riseTargetValid_ = false;
    uintptr_t riseFollowTarget_ = 0;
    bool riseFollowInitialized_ = false;
    float riseFollowX_ = 0.0f;
    float riseFollowY_ = 0.0f;

    struct RiseParticle
    {
        float x = 0.0f;
        float y = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        float age = 0.0f;
        float lifetime = 0.45f;
    };
    std::vector<RiseParticle> riseParticlesLive_;

    float anim_ = 0.0f;
    float health_ = 20.0f;
    float maxHealth_ = 20.0f;
    float lastObservedHealth_ = -1.0f;
    float estimatedHealth_ = -1.0f;
    uint64_t lastHitSequence_ = 0;
    uint64_t lastDamageApplyMs_ = 0;
    Vec3 lastPosition_{};
    bool lastPositionValid_ = false;
    float lerpedHealth_ = 20.0f;
    std::string targetName_;
    void* skinSrv_ = nullptr;
    uintptr_t skinOwner_ = 0;
    uint64_t skinHash_ = 0;
};

}
