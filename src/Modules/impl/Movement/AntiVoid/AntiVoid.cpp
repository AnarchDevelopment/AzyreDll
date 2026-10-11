#include "AntiVoid.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

namespace mc {

AntiVoid::AntiVoid()
    : Module("AntiVoid", "Boosts you back up to the last safe Y before falling into the void.", Category::Movement, 0)
{
    markHasSettings();
}

void AntiVoid::onEnable()
{
    hasSafe_ = false;
    nextRescue_ = 0;
}

void AntiVoid::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
    {
        hasSafe_ = false;
        return;
    }

    LocalPlayer local = game.localPlayer();
    if (!local.valid())
    {
        hasSafe_ = false;
        return;
    }

    Vec3 pos = local.pos();
    Vec3 vel = local.velocity();

    if (local.onGround())
    {
        safePos_ = pos;
        hasSafe_ = true;
        return;
    }

    if (!hasSafe_)
        return;

    ULONGLONG now = GetTickCount64();
    if (nextRescue_ != 0 && now < nextRescue_)
        return;

    // Caida rapida muy por debajo del ultimo punto donde se estuvo de pie.
    bool falling = vel.y < -0.60f;
    bool belowSafe = pos.y < safePos_.y - (float)distance_;
    if (falling && belowSafe)
    {
        // NO hacer setPos (crashea: VirtualProtect en pagina activa + el
        // engine puede no tener datos de chunk en la posicion nueva).
        // En su lugar: calcular la velocidad necesaria para volver a la
        // altura segura (fisica: v = sqrt(2*g*h) con margen por drag) y
        // dejar que el propio engine resuelva el arco.
        float heightNeeded = safePos_.y - pos.y;
        float dynamicVel = std::sqrt(2.0f * off::gameconst::Gravity *
                                     std::fabs(heightNeeded)) * 1.15f;
        // rescueVel_ actua como suelo minimo; cap para no lanzar al cielo.
        float boostVel = std::max(rescueVel_, dynamicVel);
        if (boostVel > 3.0f)
            boostVel = 3.0f;
        local.setVelocity({vel.x, boostVel, vel.z});
        ++rescues_;
        nextRescue_ = now + (ULONGLONG)cooldown_;
    }
}

void AntiVoid::drawSettings()
{
    widgets::CSlider("Trigger distance (blocks)##av", &distance_, 3, 30);
    widgets::CSlider("Min rescue velocity##av", &rescueVel_, 0.0f, 1.5f, "%.2f");
    widgets::CSlider("Cooldown (ms)##av", &cooldown_, 100, 2000, "%d ms");
    ImGui::TextDisabled("Rescues: %u%s", rescues_, hasSafe_ ? "" : "   (no safe position yet)");
}

MC_REGISTER_MODULE(AntiVoid);

}
