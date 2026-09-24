#include "InventoryWalk.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "GUI/Menu.hpp"
#include "SDK/Game.hpp"
#include "SDK/Offsets.hpp"

#include <imgui.h>
#include <windows.h>

namespace mc {

InventoryWalk::InventoryWalk()
    : Module("InventoryWalk", "Moverse con WASD con el inventario abierto", Category::Movement, 0)
{
    markHasSettings();
}

static bool isCursorShowing()
{
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (GetCursorInfo(&ci))
        return (ci.flags & CURSOR_SHOWING) != 0;
    return false;
}

static float axisOf(int negVk, int posVk)
{
    float v = 0.0f;
    if (GetAsyncKeyState(posVk) & 0x8000)
        v += 1.0f;
    if (GetAsyncKeyState(negVk) & 0x8000)
        v -= 1.0f;
    return v;
}

void InventoryWalk::onTick()
{
    if (menu::visible())
        return;
    if (!isCursorShowing())
        return;

    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();

    float f = axisOf('S', 'W');
    float r = axisOf('A', 'D');
    if (f == 0.0f && r == 0.0f)
        return;

    Vec3 wish = wishDirFromYaw(local.yaw(), f, r);
    if (wish.x == 0.0f && wish.z == 0.0f)
        return;

    float walk = off::gameconst::WalkSpeedBlocksPerSec / 20.0f;
    float spd = walk * speedMult_;

    Vec3 vel = local.velocity();
    vel.x = wish.x * spd;
    vel.z = wish.z * spd;

    if (jumpWithSpace_ && local.onGround() && (GetAsyncKeyState(VK_SPACE) & 0x8000))
        vel.y = off::gameconst::JumpVelocity;

    local.setVelocity(vel);
}

void InventoryWalk::drawSettings()
{
    widgets::CSlider("Speed mult##iw", &speedMult_, 1.0f, 2.0f, "%.1f");
    ImGui::Checkbox("Jump with space##iw", &jumpWithSpace_);
}

MC_REGISTER_MODULE(InventoryWalk);

}
