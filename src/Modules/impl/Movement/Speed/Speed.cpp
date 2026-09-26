#include "Speed.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>

namespace mc {

Speed::Speed()
    : Module("Speed", "Increases movement speed", Category::Movement, 'G')
{
    markHasSettings();
}

void Speed::onEnable()
{
}

void Speed::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    if (!local.onGround())
        return;

    Vec3 vel = local.velocity();
    float horizontal = vecLength2D(vel);
    if (horizontal < 0.01f)
        return;

    if (horizontal < speedPerTick_)
    {
        Vec3 dir = normalize({vel.x, 0.0f, vel.z});
        Vec3 boosted = {dir.x * speedPerTick_, vel.y, dir.z * speedPerTick_};
        local.setVelocity(boosted);
    }
}

void Speed::drawSettings()
{
    widgets::CSlider("Speed / tick##sp", &speedPerTick_, 0.22f, 1.5f, "%.3f");
}

MC_REGISTER_MODULE(Speed);

}
