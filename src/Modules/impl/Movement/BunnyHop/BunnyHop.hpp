#pragma once

#include "Modules/Module.hpp"

namespace mc {

class BunnyHop : public Module
{
public:
    BunnyHop();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"autoJump", autoJump_},
            {"requireSpace", requireSpace_},
            {"airStrafe", airStrafe_},
            {"velocity", velocity_},
            {"jumpCooldown", jumpCooldown_},
            {"airAccel", airAccel_},
            {"vMaxAir", vMaxAir_},
            {"vMaxAirAbs", vMaxAirAbs_},
            {"minAirTicks", minAirTicks_},
            {"landingPreserve", landingPreserve_},
            {"restoreOnLand", restoreOnLand_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        autoJump_ = j.value("autoJump", autoJump_);
        requireSpace_ = j.value("requireSpace", requireSpace_);
        airStrafe_ = j.value("airStrafe", airStrafe_);
        velocity_ = j.value("velocity", velocity_);
        jumpCooldown_ = j.value("jumpCooldown", jumpCooldown_);
        airAccel_ = j.value("airAccel", airAccel_);
        vMaxAir_ = j.value("vMaxAir", vMaxAir_);
        vMaxAirAbs_ = j.value("vMaxAirAbs", vMaxAirAbs_);
        minAirTicks_ = j.value("minAirTicks", minAirTicks_);
        landingPreserve_ = j.value("landingPreserve", landingPreserve_);
        restoreOnLand_ = j.value("restoreOnLand", restoreOnLand_);
    }

private:
    void resetState();

    bool autoJump_ = true;
    bool requireSpace_ = false;
    bool airStrafe_ = true;
    float velocity_ = 0.42f;
    int jumpCooldown_ = 3;
    float airAccel_ = 0.10f;
    float vMaxAir_ = 0.60f;
    float vMaxAirAbs_ = 2.00f;
    int minAirTicks_ = 2;
    float landingPreserve_ = 0.95f;
    bool restoreOnLand_ = true;

    int lastOnGround_ = 1;
    int airTicks_ = 0;
    int tickCount_ = 0;
    int lastJumpTick_ = -100;
    float savedAirVx_ = 0.0f;
    float savedAirVz_ = 0.0f;
    float savedAirSpeed_ = 0.0f;
};

}
