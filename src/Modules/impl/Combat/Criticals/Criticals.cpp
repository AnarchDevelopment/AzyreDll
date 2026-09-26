#include "Criticals.hpp"

#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

namespace mc {

Criticals::Criticals()
    : Module("Criticals", "Critical hits by toggling OnGround", Category::Combat, 0)
{
    markHasSettings();
}

void Criticals::onDisable()
{
    if (wasAttacking_)
    {
        Game& game = Game::get();
        if (game.localFound())
            game.localPlayer().setOnGround(true);
    }
    wasAttacking_ = false;
    phase_ = 0;
}

void Criticals::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    bool attacking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 || local.leftClickFlag();

    if (onlyWhileAttacking_ && !attacking)
    {
        if (wasAttacking_)
            local.setOnGround(true);
        wasAttacking_ = false;
        phase_ = 0;
        return;
    }

    wasAttacking_ = attacking;

    if (!local.onGround())
    {
        phase_ = 0;
        return;
    }

    phase_ = (phase_ + 1) % 2;
    if (phase_ == 1)
    {
        local.setOnGround(false);
        Vec3 vel = local.velocity();
        if (vel.y > -0.1f)
        {
            vel.y = -0.1f;
            local.setVelocity(vel);
        }
    }
    else
    {
        local.setOnGround(true);
    }
}

void Criticals::drawSettings()
{
    ImGui::Checkbox("Only while attacking##cr", &onlyWhileAttacking_);
}

MC_REGISTER_MODULE(Criticals);

}
