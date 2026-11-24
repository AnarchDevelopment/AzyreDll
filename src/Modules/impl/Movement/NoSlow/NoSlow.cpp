#include "NoSlow.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "Framework/Log.hpp"
#include "Framework/Memory.hpp"
#include "SDK/Core/Game.hpp"

#include <imgui.h>
#include <windows.h>

#include <cmath>

namespace mc {

namespace {

// Teclas de movimiento (WASD). El slow solo pesa si hay desplazamiento
// intencionado: sin input no se escribe velocidad (nada de aceleron parado).
bool keyDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

void readMoveKeys(float& fwd, float& str)
{
    fwd = 0.0f;
    str = 0.0f;
    if (keyDown('W'))
        fwd += 1.0f;
    if (keyDown('S'))
        fwd -= 1.0f;
    if (keyDown('D'))
        str += 1.0f;
    if (keyDown('A'))
        str -= 1.0f;
}

} // namespace

NoSlow::NoSlow()
    : Module("NoSlow", "Cancels the item-use slowdown without adding speed", Category::Movement, 0)
{
    markHasSettings();
}

void NoSlow::onEnable()
{
    freeSpeed_ = 0.0f;
    writes_ = 0;
}

void NoSlow::onDisable()
{
    freeSpeed_ = 0.0f;
    usingItem_ = false;
}

void NoSlow::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
    {
        freeSpeed_ = 0.0f;
        usingItem_ = false;
        return;
    }

    LocalPlayer local = game.localPlayer();
    Vec3 vel = local.velocity();
    const float speed = vecLength2D(vel);
    if (!std::isfinite(speed) || !std::isfinite(vel.y))
        return;

    float fwd = 0.0f;
    float str = 0.0f;
    readMoveKeys(fwd, str);
    const bool hasInput = (std::fabs(fwd) > 0.01f || std::fabs(str) > 0.01f);

    // --- Referencia: velocidad horizontal LIBRE ---
    // Solo se muestrea cuando el jugador no esta usando item. Con input y
    // movimiento real, esa es la velocidad que el slow intenta recortar;
    // sin input se deja decaer para no arrastrar inercia de hace rato.
    usingItem_ = local.isUsingItem();
    if (!usingItem_)
    {
        if (hasInput && speed > 0.02f)
            freeSpeed_ = speed;
        else if (!hasInput && freeSpeed_ > 0.0f)
            freeSpeed_ *= 0.85f;
        return;
    }

    if (onlyGround_ && !local.onGround())
        return;
    if (!hasInput)
        return; // usando item parado: el vanilla ya lo deja quieto

    const float target = std::min(freeSpeed_, maxSpeed_);
    if (target < 0.02f)
        return;

    // Ya va igual o mas rapido: no tocar. Asi nunca se suma velocidad.
    if (speed >= target - 0.005f)
        return;

    Vec3 dir = wishDirFromYaw(local.yaw(), fwd, str);
    if (vecLength2D(dir) < 0.01f)
        return;

    // Reposicion del tick: solo X/Z. La Y queda intacta (gravedad y salto
    // siguen siendo del juego).
    vel.x = dir.x * target;
    vel.z = dir.z * target;
    local.setVelocity(vel);

    if (debug_)
    {
        ++writes_;
        if ((writes_ % 20) == 1)
            MC_LOG("[NoSlow] restore %.3f -> %.3f (free=%.3f cap=%.3f)",
                   speed, target, freeSpeed_, maxSpeed_);
    }
}

void NoSlow::drawSettings()
{
    widgets::CSlider("Max speed##ns", &maxSpeed_, 0.15f, 0.50f, "%.3f");
    ImGui::TextDisabled("0.216 walk / 0.30 sprint (blocks per tick)");
    widgets::Checkbox("Only on ground##ns", &onlyGround_);
    widgets::Checkbox("Debug##ns", &debug_);

    ImGui::Separator();
    ImGui::TextDisabled("free=%.3f  using=%s  writes=%d",
                        freeSpeed_, usingItem_ ? "yes" : "no", writes_);
}

MC_REGISTER_MODULE(NoSlow);

}
