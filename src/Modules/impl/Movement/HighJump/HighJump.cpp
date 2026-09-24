#include "HighJump.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>

namespace mc {

HighJump::HighJump()
    : Module("HighJump", "Salto mas alto al impulsarse", Category::Movement, 0)
{
    markHasSettings();
}

void HighJump::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    Vec3 vel = local.velocity();
    if (vel.y > 0.05f && vel.y < boost_ && (local.onGround() || vel.y < 0.50f))
    {
        vel.y = boost_;
        local.setVelocity(vel);
    }
}

void HighJump::drawSettings()
{
    widgets::CSlider("Jump velocity##hj", &boost_, 0.42f, 1.20f, "%.2f");
}

MC_REGISTER_MODULE(HighJump);

}
