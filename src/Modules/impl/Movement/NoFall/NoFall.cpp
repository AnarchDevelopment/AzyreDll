#include "NoFall.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>

namespace mc {

NoFall::NoFall()
    : Module("NoFall", "Previene el dano de caida", Category::Movement, 0)
{
    markHasSettings();
}

void NoFall::onTick()
{
    if (!ghostGround_)
        return;

    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    Vec3 vel = local.velocity();
    if (!local.onGround() && vel.y <= ghostThreshold_)
        local.setOnGround(true);
}

void NoFall::drawSettings()
{
    ImGui::Checkbox("Ghost ground while falling", &ghostGround_);
    widgets::CSlider("Vy threshold", &ghostThreshold_, -4.0f, -0.5f, "%.2f");
}

MC_REGISTER_MODULE(NoFall);

}
