#pragma once

#include "Modules/Module.hpp"

#include "Framework/Math.hpp"

#include <windows.h>

namespace mc {

// Backtrack — "Legit Reach" (Rise 6.9.5) para Bedrock, edicion sin hooks.
//
// La version anterior parcheaba los slots 6/15 de la vtable de RemotePlayer
// durante el hold, con tres problemas mortales:
//   1. Los detours corrian en el GAME thread, fuera de TODA guardia SEH
//      (cualquier falta = crash duro del juego, no module-disable).
//   2. 0xE64588 es la vtable secundaria de VRegionListener dentro de
//      RemotePlayer (+0xFD8), no una vtable de Actor. Sus slots se solapan
//      con RemotePlayer y no se deben tratar como jugadores independientes.
//   3. menu::visible() solo cubre el menu del cheat: los clicks en el
//      INVENTARIO del juego contaban como "ataques" -> holds (y patches de
//      vtable) arrancaban mientras la UI llamaba virtuales.
//
// Esta edicion NO hookea ni parchea nada:
//   * Todo vive en el render thread (onTick 20Hz = maquina de estados,
//     onRender por frame = refresh del freeze + ghost), cubierto por las
//     guardias SEH del ModuleManager: una falta desactiva el modulo, nunca
//     tumba el juego.
//   * El freeze reescribe el estado del actor cada frame via mem::rawWrite
//     (VirtualQuery + SEH): mPosition (+0x0C), el target de red del moveTo
//     (+0xD08) y el AABB COMPLETO trasladado geometricamente al anchor —
//     los offsets del box se leen del propio actor vivo, asi el hitbox
//     queda siempre consistente (el bug del enfoque original de writes).
//   * "attacking" exige el cursor del sistema OCULTO (GetCursorInfo, como
//     InventoryWalk): los clicks en inventario/contenedor/chat/pause no
//     inician holds y cualquier hold activo se despacha al abrir pantalla.
//
// Se conserva la semantica LegitReach de Rise: ventana min..max, max hold
// de maxPingSpoof ms, smart (suelta si la real quedo mas cerca), guard de
// teleport, dispatch con snap-back al soltar y el "Render Real Location"
// (caja en la posicion real, encuadre + suavizado del ESP).
class Backtrack : public Module
{
public:
    Backtrack();

    void onEnable() override;
    void onDisable() override;
    void onShutdown() override;
    void onTick() override;
    void onRender() override;
    void drawSettings() override;

    std::string suffix() const override
    {
        return std::to_string((int)maxPingSpoof_) + "ms";
    }

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    // Ajustes (LegitReach.java: maxPingSpoof 50..10000, ventana 2.5..6.0)
    float maxPingSpoof_ = 1000.0f; // ms maximos de hold (blink) por ataque
    float minDist_ = 2.5f;         // el target debe estar al menos a esta dist
    float maxDist_ = 6.0f;         // despacha si la posicion real supera esto
    bool smart_ = true;            // soltar si la real esta mas cerca que la shown
    bool renderReal_ = true;       // Render Real Location (realTargetBoundingBox)

    // Hold activo. Todo el estado vive en el render thread: sin atomics.
    uintptr_t holdAddr_ = 0;
    ULONGLONG holdStart_ = 0;
    Vec3 anchor_{};   // posicion visible donde el target queda clavado
    Vec3 realPos_{};  // realTargetPosition de LegitReach (paquete-exacto)

    // Render Real Location — la caja se dibuja siempre que hay target.
    // Encuadre y suavizado IDENTICOS al ESP: anchor en espacio de pies
    // (+0xB4), altura height() (+0xD4) y smooth::interp (hand-off seamless,
    // QPC, sin delay). El buffer de historia de 150ms que usaba antes
    // desfasaba la caja 150ms respecto al modelo.
    Vec3 ghostPos_{};
    bool hasTarget_ = false;

    // Diagnostico en drawSettings
    uintptr_t lastTargetAddr_ = 0;
    float lastTargetDist_ = 0.0f;
    bool lastAttacking_ = false;

    // Dispatch de Rise: snap-back a la posicion real + fin del hold.
    void releaseHold();
};

}
