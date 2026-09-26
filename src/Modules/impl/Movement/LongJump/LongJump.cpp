#include "LongJump.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>

namespace mc {

LongJump::LongJump()
    : Module("LongJump", "Multiplies horizontal momentum when jumping.", Category::Movement, 0)
{
    markHasSettings();
}

void LongJump::onEnable()
{
    wasOnGround_ = true;
}

void LongJump::onDisable()
{
    wasOnGround_ = true;
}

void LongJump::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();

    Vec3 vel = local.velocity();
    bool onGround = local.onGround();
    float hSpeed = vecLength2D(vel);

    // Salto recien iniciado: estaba en el suelo y la velocidad vertical es de impulso.
    bool jumped = wasOnGround_ && !onGround && vel.y > off::gameconst::JumpVelocity * 0.75f;

    if (jumped && hSpeed > 0.05f)
    {
        // Sprint camina ~0.28 bloques/tick, caminar normal ~0.22.
        if (!onlySprint_ || hSpeed > 0.24f)
            local.setVelocityXZ({vel.x * multiplier_, vel.y, vel.z * multiplier_});
    }

    wasOnGround_ = onGround;
}

void LongJump::drawSettings()
{
    widgets::CSlider("Multiplier##lj", &multiplier_, 1.1f, 3.0f, "%.2f");
    ImGui::Checkbox("Only sprinting##lj", &onlySprint_);
}

MC_REGISTER_MODULE(LongJump);

}
