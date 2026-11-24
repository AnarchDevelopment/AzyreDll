#pragma once

#include "Modules/Module.hpp"

#include "Framework/Math.hpp"

#include <windows.h>

namespace mc {

class Actor;

// Backtrack — lag compensation real: el target se congela y despues sigue su
// trayectoria con retraso.
//
// Referencia: OpenSlinky (src/logic/combat/backtrack.cpp) retiene los paquetes
// de movimiento del target hasta `delay` ms y los libera despues; en pantalla
// el enemigo queda donde estaba y luego avanza por su trayectoria atrasado.
// Aqui no hay paquetes que retener (no hay hook de recepcion por entidad), asi
// que se consigue lo mismo escribiendo al actor cada frame en la posicion que
// tenia hace `delayMs` (Features/LagHistory). Resultado identico al de
// OpenSlinky: el hitbox y el modelo van `delayMs` en el pasado y siguen
// moviendose, no clavados en un anchor fijo (eso era "knockback delay").
//
// La caja NO es el target: es el indicador de su posicion REAL (la que tiene
// el server ahora mismo), tomada del target de red +0xD08 antes del rewrite.
class Backtrack : public Module
{
public:
    Backtrack();

    void onEnable() override;
    void onDisable() override;
    void onShutdown() override;
    void onTick() override;
    void onRender() override;
    void onHurt(void* actor, void* source, float& damage, bool& cancel) override;
    void drawSettings() override;

    std::string suffix() const override
    {
        return std::to_string((int)delayMs_) + "ms";
    }

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    // Ajustes (equivalencias con OpenSlinky)
    float delayMs_ = 200.0f;   // "delay": cuanto atras va el target
    float minDist_ = 2.5f;     // "target_dist" (min)
    float maxDist_ = 6.0f;     // "target_dist" (max)
    bool requireMouse_ = true; // "require_mouse_pressed"
    bool disableOnHit_ = true; // "disable_on_hit": soltar al recibir knockback
    bool renderReal_ = true;   // "render_remote_pos": caja en la posicion real
    bool debug_ = false;

    // Estado (render thread)
    uintptr_t targetAddr_ = 0;
    Vec3 realPos_{};      // posicion REAL del target (ultimo paquete)
    Vec3 lastWritten_{};  // ultimo valor escrito en el actor
    Vec3 shownPos_{};     // posicion historica ya suavizada (lo que se escribe)
    Vec3 ghostPos_{};     // caja suavizada
    bool wroteOnce_ = false;
    bool active_ = false;
    float lastDist_ = 0.0f;
    bool attacking_ = false;
    const char* releaseReason_ = "idle";

    void stopLag(const char* why);
    void drawRealBox();
};

}
