#include "NoSlow.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>

namespace mc {

NoSlow::NoSlow()
    : Module("NoSlow", "Mantiene velocidad al usar items", Category::Movement, 0)
{
    markHasSettings();
}

void NoSlow::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    if (!local.isUsingItem())
        return;
    if (onlyGround_ && !local.onGround())
        return;

    Vec3 vel = local.velocity();
    float speed = vecLength2D(vel);
    if (speed < 0.01f || speed >= restoreSpeed_)
        return;

    Vec3 dir = normalize({vel.x, 0.0f, vel.z});
    Vec3 restored = {dir.x * restoreSpeed_, vel.y, dir.z * restoreSpeed_};
    local.setVelocity(restored);
}

void NoSlow::drawSettings()
{
    widgets::CSlider("Walk speed##ns", &restoreSpeed_, 0.15f, 0.50f, "%.3f");
    ImGui::Checkbox("Only on ground##ns", &onlyGround_);
}

MC_REGISTER_MODULE(NoSlow);

}
