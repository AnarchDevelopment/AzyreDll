#pragma once

#include "Modules/Module.hpp"
#include "Framework/Math.hpp"

#include <windows.h>

namespace mc {

class LocalPlayer;
class Actor;

// AutoClicker nativo: no inyecta input del sistema (SendInput). Repite la
// cadena que el juego ejecuta en un click real:
//   - entidad bajo la mirada -> InteractPacket::sendAttack + Actor::swing
//     (el mismo paquete de red que el click nativo, sin la prediccion local
//     pesada de GameMode::attack).
//   - bloque bajo la mirada  -> GameMode::startDestroyBlock (slot 1) al abrir
//     el ciclo y continueDestroyBlock (slot 3) cada tick mientras siga activo;
//     stopDestroyBlock (slot 4) al soltar.
//   - aire                   -> solo Actor::swing.
//   - boton derecho          -> GameMode::useItemOn (slot 12) sobre el bloque
//     apuntado o useItem (slot 11) en el aire.
class AutoClicker : public Module
{
public:
    AutoClicker();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void onFrame(float dt) override;
    void drawSettings() override;

    std::string suffix() const override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    double intervalMs() const;
    void doLeftClick(const LocalPlayer& local);
    void doRightClick(const LocalPlayer& local);
    Actor entityUnderCrosshair(const LocalPlayer& local, const Vec3& eye,
                               const Vec3& dir) const;
    void stopLeftCycle(bool sendStop);

    int cps_ = 12;
    int jitter_ = 0;          // % de variacion del intervalo
    bool holdClick_ = true;   // solo mientras el boton fisico este apretado
    bool left_ = true;
    bool right_ = false;
    bool swing_ = true;
    bool attackEntities_ = true;
    bool breakBlocks_ = true;
    float range_ = 4.5f;
    // Grados de tolerancia cuando el rayo no toca el AABB de red del actor
    // (desfase tick/render). 0 = solo hitbox exacta.
    float aimTolerance_ = 10.0f;
    bool debug_ = false;

    double lastLeftMs_ = 0.0;
    double nextLeftDelayMs_ = 0.0;
    double lastRightMs_ = 0.0;
    double nextRightDelayMs_ = 0.0;

    // Ciclo de rotura abierto por el click (pos en curso del GameMode).
    bool destroyActive_ = false;
    BlockPos destroyPos_{};
    int destroyFace_ = 0;
};

}
