#pragma once

#include "Modules/Module.hpp"

#include "Framework/Math.hpp"
#include "SDK/Offsets.hpp"

#include <map>
#include <vector>
#include <windows.h>

namespace mc {

// Backtrack — LiquidBounce Legacy approach adapted for Bedrock.
// Records entity positions passively every tick (read-only, zero crash
// risk). On the attack click, momentarily sets the entity position to a
// past position so the game's raycast and reach check pass against the
// backtracked hitbox, then the game's own MoveActorAbsolutePacket from
// the server restores the real position on the next network update.
//
// Key differences from LiquidBounce Modern (which we can't do): Modern
// intercepts and delays incoming packets, so the entity NATURALLY stays
// at its old position. Without an incoming packet hook, we approximate
// the effect by writing/letting-restore in the same window.
//
// "Smart" mode (default): only applies the backtrack when the past
// position was CLOSER to the player than the current one — prevents
// useless backtracking when the target is running toward you.
class Backtrack : public Module
{
public:
    Backtrack();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void onRender() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    struct Entry
    {
        Vec3 pos;
        ULONGLONG tick;
    };

    // Historial por direccion del actor.
    std::map<uintptr_t, std::vector<Entry>> history_;
    static constexpr int kMaxHistory = 40;

    int ticks_ = 3;         // cuanto atras rebobinar (1 tick ~ 50ms)
    bool smart_ = true;     // solo aplicar si la pos vieja esta mas cerca
    bool visualize_ = true; // dibujar la caja backtrack
    float minDist_ = 2.0f;  // distancia minima al target
    float maxDist_ = 3.0f;  // distancia maxima al target

    bool prevClick_ = false;
    Vec3 lastAppliedPos_ = {};
    bool hasApplied_ = false;
};

}
