#include "Phase.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "Framework/Memory.hpp"
#include "GUI/Menu.hpp"
#include "SDK/ChatHook.hpp"
#include "SDK/Classes/BlockSource.hpp"
#include "SDK/Classes/LocalPlayer.hpp"
#include "SDK/Game.hpp"

#include <windows.h>
#include <imgui.h>

#include <cmath>
#include <cstdio>

namespace mc {

namespace {

bool keyDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool isCursorShowing()
{
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (GetCursorInfo(&ci))
        return (ci.flags & CURSOR_SHOWING) != 0;
    return false;
}

float axisOf(int negVk, int posVk)
{
    float v = 0.0f;
    if (GetAsyncKeyState(posVk) & 0x8000)
        v += 1.0f;
    if (GetAsyncKeyState(negVk) & 0x8000)
        v -= 1.0f;
    return v;
}

bool isLiquidId(int id)
{
    return id == 8 || id == 9 || id == 10 || id == 11;
}

bool isSolidId(int id)
{
    return id > 0 && !isLiquidId(id);
}

} // namespace

Phase::Phase()
    : Module("Phase", "Teleports through walls you walk into",
             Category::Movement, 0)
{
    markHasSettings();
}

void Phase::onEnable()
{
    lastPhaseMs_ = 0;
}

void Phase::displayDebug(const std::string& msg)
{
    if (!debug_)
        return;
    chathook::display("§7[§bPhase§7]§f " + msg);
}

void Phase::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    if (!local.valid())
        return;

    BlockSource bs = game.blockSource();
    if (!bs.valid())
        return;

    tickPhase(local, bs);
}

void Phase::tickPhase(LocalPlayer& local, const BlockSource& bs)
{
    if (menu::visible() || isCursorShowing())
        return;

    if (mode_ == kSneakOnly && !keyDown(VK_SHIFT))
        return;

    Vec3 feet = local.pos();       // AABB min (pies)
    Vec3 exact = local.posExact(); // mPosition (centro horizontal)
    if (!std::isfinite(feet.x) || !std::isfinite(feet.y) || !std::isfinite(feet.z) ||
        !std::isfinite(exact.x) || !std::isfinite(exact.y) || !std::isfinite(exact.z))
        return;

    float fwd = axisOf('S', 'W');
    float str = axisOf('A', 'D');
    if (fwd == 0.0f && str == 0.0f)
        return;

    Vec3 dir = wishDirFromYaw(local.yaw(), fwd, str);
    if (vecLength2D(dir) < 0.01f)
        return;

    int feetCellY = (int)std::floor(feet.y + 0.01f);
    int ownId = bs.getBlockId((int)std::floor(exact.x), feetCellY, (int)std::floor(exact.z));
    if (isLiquidId(ownId))
        return; // en liquidos lo maneja el vanilla

    // Marcha a lo largo de la direccion: localiza la pared (ultima celda
    // solida) y la primera salida con despeje CONFIRMADO (id==0) a 2 celdas
    // verticales, justo despues de la pared.
    float lastSolid = -1.0f;
    float destT = -1.0f;
    for (float t = 0.2f; t <= distance_ + 0.001f; t += 0.1f)
    {
        int cx = (int)std::floor(exact.x + dir.x * t);
        int cz = (int)std::floor(exact.z + dir.z * t);
        int id0 = bs.getBlockId(cx, feetCellY, cz);
        int id1 = bs.getBlockId(cx, feetCellY + 1, cz);

        if (isSolidId(id0) || isSolidId(id1))
        {
            lastSolid = t;
            continue;
        }

        if (id0 == 0 && id1 == 0 && lastSolid >= 0.0f && t >= lastSolid + 0.35f)
        {
            destT = t;
            break;
        }
    }

    if (lastSolid < 0.0f)
        return; // sin pared: no hay nada que atravesar
    if (destT < 0.0f)
        return; // sin salida confirmada dentro del alcance

    ULONGLONG now = GetTickCount64();
    if (now - lastPhaseMs_ < (ULONGLONG)cooldownMs_)
        return;

    uintptr_t a = local.address();
    if (!a || !mem::isReadable(a, 0xD8))
        return;

    Vec3 np{exact.x + dir.x * destT, exact.y, exact.z + dir.z * destT};

    // Mismo patron que el Step Clip: AABB trasladado + mPosition (+0x0C) +
    // move target (+0xD08), las 3 escrituras juntas.
    float box[6] = {};
    if (!mem::rawRead(box, a + off::player::AabbMinX, sizeof(box)))
        return;
    float offMin[3] = {box[0] - exact.x, box[1] - exact.y, box[2] - exact.z};
    float offMax[3] = {box[3] - exact.x, box[4] - exact.y, box[5] - exact.z};

    float moved[6] = {
        np.x + offMin[0], np.y + offMin[1], np.z + offMin[2],
        np.x + offMax[0], np.y + offMax[1], np.z + offMax[2],
    };
    if (!mem::rawWrite(a + off::player::AabbMinX, moved, sizeof(moved)))
        return;

    mem::write<float>(a + off::player::PosTrueX, np.x);
    mem::write<float>(a + off::player::PosTrueY, np.y);
    mem::write<float>(a + off::player::PosTrueZ, np.z);
    mem::write<float>(a + off::player::MoveTargetX, np.x);
    mem::write<float>(a + off::player::MoveTargetY, np.y);
    mem::write<float>(a + off::player::MoveTargetZ, np.z);

    lastPhaseMs_ = now;

    char buf[80];
    snprintf(buf, sizeof(buf), "phase %.2f blocks", destT);
    displayDebug(buf);
}

void Phase::drawSettings()
{
    const char* modes[] = {"Always", "Sneak only"};
    widgets::CCombo("Mode##ph", &mode_, modes, 2);
    widgets::CSlider("Distance##ph", &distance_, 1.0f, 4.0f, "%.2f");
    widgets::CSlider("Cooldown##ph", &cooldownMs_, 50, 500, "%d ms");
    widgets::Checkbox("Debug##ph", &debug_);
    ImGui::TextDisabled("Walk into a wall to clip through it");
    ImGui::TextDisabled("Sneak only = classic vanilla-bypass clip");
}

MC_REGISTER_MODULE(Phase);

}
