#pragma once

#include "Modules/Module.hpp"
#include "Framework/Math.hpp"

#include <cstdint>

namespace mc {

class LocalPlayer;
class GameMode;

class FastBreak : public Module
{
public:
    FastBreak();

    void onFrame(float dt) override;
    void drawSettings() override;

    std::string suffix() const override
    {
        if (mode_ == 0)
            return "Instant";
        char buf[16];
        snprintf(buf, sizeof(buf), "x%.0f", speed_);
        return buf;
    }

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"mode", mode_},
            {"speed", speed_},
            {"requireClick", requireClick_},
            {"onlySurvival", onlySurvival_},
            {"debug", debug_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        mode_ = j.value("mode", mode_);
        speed_ = j.value("speed", speed_);
        requireClick_ = j.value("requireClick", requireClick_);
        onlySurvival_ = j.value("onlySurvival", onlySurvival_);
        debug_ = j.value("debug", debug_);
    }

private:
    bool canRun(const LocalPlayer& lp, const GameMode& gm) const;

    // 0 = Instant (progreso a 1.0), 1 = Multiplier (progreso * speed).
    int mode_ = 0;
    float speed_ = 3.0f;
    // Solo con el click izquierdo mantenido (evita romper tras soltar).
    bool requireClick_ = true;
    // El progreso forzado solo tiene sentido donde el cliente predice la rotura.
    bool onlySurvival_ = true;
    bool debug_ = false;

    uint64_t lastDebugMs_ = 0;
    BlockPos lastTarget_{};
};

}
