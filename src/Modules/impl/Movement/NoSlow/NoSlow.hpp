#pragma once

#include "Modules/Module.hpp"
#include "Framework/Math.hpp"

namespace mc {

// NoSlow — cancela el slowdown por usar item (comer, bloquear, tensar arco).
//
// El juego aplica el slow cada tick como un factor sobre la velocidad
// horizontal. Reponer un valor fijo (la version anterior) aceleraba de mas:
// caminar a 0.13 con el slow puesto saltaba a 0.216 de golpe — un speedhack
// encubierto. Ahora se guarda la velocidad horizontal LIBRE (la del ultimo
// tick sin item y con input) y solo se rellena esa perdida mientras el
// jugador se mueve; sin input no se escribe nada, y nunca se supera el techo
// configurado. La componente Y (gravedad/salto) no se toca.
class NoSlow : public Module
{
public:
    NoSlow();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void drawSettings() override;

    std::string suffix() const override
    {
        return usingItem_ ? "Using" : "Idle";
    }

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"maxSpeed", maxSpeed_},
            {"onlyGround", onlyGround_},
            {"debug", debug_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        maxSpeed_ = clampf(j.value("maxSpeed", maxSpeed_), 0.15f, 0.50f);
        onlyGround_ = j.value("onlyGround", onlyGround_);
        debug_ = j.value("debug", debug_);
    }

private:
    // Techo de la reposicion (bloques/tick). 0.30 = sprint de Bedrock.
    float maxSpeed_ = 0.30f;
    bool onlyGround_ = false;
    bool debug_ = false;

    // Ultima velocidad horizontal alcanzada sin el slow.
    float freeSpeed_ = 0.0f;
    bool usingItem_ = false;
    int writes_ = 0;
};

}
