#include "FastPlace.hpp"

#include "Features/Raycast.hpp"
#include "Framework/Math.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Widgets.hpp"
#include "SDK/Classes/GameMode.hpp"
#include "SDK/Classes/ItemInstance.hpp"
#include "SDK/Classes/LocalPlayer.hpp"
#include "SDK/Classes/MoveInputHandler.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>
#include <cmath>

namespace mc {

FastPlace::FastPlace()
    : Module("FastPlace", "Removes placement delay while holding right-click", Category::Misc, 0)
{
    markHasSettings();
}

void FastPlace::onEnable()
{
    tickCount_ = 0;
}

void FastPlace::onTick()
{
    // Do not place if the menu or an ImGui window is active
    if (mc::menu::visible() || ImGui::GetIO().WantCaptureMouse)
        return;

    // Only active while holding Right Mouse Button
    if ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) == 0)
    {
        tickCount_ = 0;
        return;
    }

    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer lp = game.localPlayer();
    if (!lp.valid())
        return;

    ItemInstance held = lp.heldItem();
    if (!held.valid() || held.count() <= 0)
        return;

    bool isBlock = held.isBlockItem();
    if (!isBlock)
    {
        int id = held.itemId();
        if (id > 0 && id < 256)
            isBlock = true;
    }

    bool isProj = false;
    if (!isBlock && projectiles_)
    {
        int id = held.itemId();
        // 332=Snowball, 344=Egg, 368=EnderPearl, 384=ExpBottle, 438=SplashPotion
        if (id == 332 || id == 344 || id == 368 || id == 384 || id == 438)
            isProj = true;
    }

    if (blocksOnly_ && !isBlock && !isProj)
        return;

    // Reset native input cooldown as well
    MoveInputHandler moveInput = game.moveInput();
    if (moveInput.valid())
    {
        moveInput.setClickCooldown(delay_);
        if (delay_ == 0)
            moveInput.resetClickCounter();
    }

    // Delay throttle: delay_ == 0 means every tick (20 bps)
    tickCount_++;
    if (delay_ > 0 && (tickCount_ % (delay_ + 1)) != 0)
        return;

    BlockSource bs = game.blockSource();
    if (!bs.valid())
        return;

    Vec3 eye = lp.eyePos();
    float yaw = lp.yaw();
    float pitch = lp.pitch();
    float yR = deg2rad(yaw);
    float pR = deg2rad(pitch);
    Vec3 dir{
        -std::sin(yR) * std::cos(pR),
        -std::sin(pR),
        std::cos(yR) * std::cos(pR)
    };

    RaycastHit hit = raycastBlock(bs, eye, dir, reach_);
    if (!hit.hit || hit.face < 0 || hit.face > 5)
        return;

    float cx = 0.0f, cy = 0.0f, cz = 0.0f;
    switch (hit.face)
    {
    case 0: cx = (float)hit.pos.x + 0.5f; cy = (float)hit.pos.y;        cz = (float)hit.pos.z + 0.5f; break;
    case 1: cx = (float)hit.pos.x + 0.5f; cy = (float)hit.pos.y + 1.0f; cz = (float)hit.pos.z + 0.5f; break;
    case 2: cx = (float)hit.pos.x + 0.5f; cy = (float)hit.pos.y + 0.5f; cz = (float)hit.pos.z;        break;
    case 3: cx = (float)hit.pos.x + 0.5f; cy = (float)hit.pos.y + 0.5f; cz = (float)hit.pos.z + 1.0f; break;
    case 4: cx = (float)hit.pos.x;        cy = (float)hit.pos.y + 0.5f; cz = (float)hit.pos.z + 0.5f; break;
    case 5: cx = (float)hit.pos.x + 1.0f; cy = (float)hit.pos.y + 0.5f; cz = (float)hit.pos.z + 0.5f; break;
    default: return;
    }
    Vec3 click{cx, cy, cz};

    // Right-click COMPLETO del juego: GameMode::useItemOn con la firma
    // confirmada — paquete al server + interact + place local + consume.
    bool ok = mc::placeBlockFull(held, hit.pos, (uint8_t)hit.face, click,
                                 lp.address());
    if (!ok)
        ok = mc::blockItemUseOn(held, hit.pos, (uint8_t)hit.face, click,
                                lp.address());
}

void FastPlace::drawSettings()
{
    widgets::CSlider("Delay##fp", &delay_, 0, 4, "%d ticks");
    widgets::CSlider("Reach##fp", &reach_, 3.0f, 6.0f, "%.1f m");
    widgets::Checkbox("Blocks only##fp", &blocksOnly_);
    widgets::Checkbox("Allow projectiles##fp", &projectiles_);
}

MC_REGISTER_MODULE(FastPlace);

}
