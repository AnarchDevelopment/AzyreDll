#pragma once

#include "Modules/Module.hpp"

#include "SDK/Classes/BlockSource.hpp"

namespace mc {

class LocalPlayer;
struct Vec3;

// Scaffold — puerto de la logica de Solstice 1.21 (vastrakai):
//   * mStartY con lock: el Y del puente nunca sube mientras avanzas (salvo
//     jump/tower) — el piso de la estructura.
//   * getPlacePos: celda a mStartY en la DIRECCION DE VISTA con extend
//     (0 = bajo los pies); si no tiene cara colocable, getClosestPlacePos
//     busca en espiral la celda valida mas cercana dentro del range.
//   * Extend loop: si la celda base no es aire, avanza hasta maxExtend
//     (solo con teclas de movimiento presionadas).
//   * getBlockPlaceFace: 6 vecinos (abajo primero, luego laterales,
//     arriba al final) — la cara del soporte sobre la que se coloca.
//   * Places por tick + tower Velocity/Clip.
// La colocacion va por GameMode::useItemOn real (sub_1405113F0): el
// right-click COMPLETO del juego — paquete al server, place local y
// consume del item.
class Scaffold : public Module
{
public:
    enum Mode
    {
        kNormal = 0,
        kTelly = 1,
        kRewind = 2,
    };

    Scaffold();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void drawSettings() override;

    std::string suffix() const override
    {
        if (mode_ == kTelly)
            return "Telly";
        if (mode_ == kRewind)
            return "Rewind";
        if (towerMode_ == 1)
            return "Vel";
        if (towerMode_ == 2)
            return "Clip";
        return "";
    }

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    bool tickPlace(const LocalPlayer& lp, const BlockSource& bs);

    // Ajustes (Solstice: mPlaces/mExtend/mRange/mLockY/mAvoidUnderplace/
    // mTowerMode/mTowerSpeed/mAllowMovement)
    int mode_ = kNormal;
    float places_ = 1.0f;
    float extend_ = 3.0f;
    float range_ = 5.0f;
    bool lockY_ = false;
    bool avoidUnderplace_ = false;
    int towerMode_ = 0; // 0=Vanilla, 1=Velocity, 2=Clip
    float towerSpeed_ = 8.5f;
    bool allowMovement_ = false;
    int switchDelayMs_ = 0;
    int rotateMode_ = 0; // 0=None, 1=Normal, 2=Down, 3=Backwards, 4=Reverse
    float tellyLead_ = 0.25f;  // predicción XZ de aterrizaje (s)
    int rewindDelayMs_ = 80;   // cadencia de place en RewindSide

    // Estado (Solstice: mStartY/mIsTowering/mLastSlot/mLastSwitchTime)
    float mStartY_ = 0.0f;
    bool mIsTowering_ = false;
    int mLastSlot_ = -1;
    unsigned long long lastSwapMs_ = 0;
    unsigned long long lastPlaceMs_ = 0;
    int failStreak_ = 0; // places fallidos seguidos -> swap forzado
    int lastStrafe_ = 1; // +1 derecha (D), -1 izquierda (A) — RewindSide
    unsigned long long lastRewindMs_ = 0;
};

}
