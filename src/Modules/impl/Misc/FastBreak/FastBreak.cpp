#include "FastBreak.hpp"

#include "Framework/Log.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Widgets.hpp"
#include "SDK/Core/Game.hpp"
#include "SDK/Engine/VTable/VTables.hpp"
#include "SDK/Entity/LocalPlayer.hpp"
#include "SDK/Gameplay/GameMode.hpp"

#include <imgui.h>
#include <windows.h>

#include <algorithm>

namespace mc {

FastBreak::FastBreak()
    : Module("FastBreak", "Speeds up block breaking through GameMode's own destroy tick",
             Category::Misc, 0)
{
    markHasSettings();
}

bool FastBreak::canRun(const LocalPlayer& lp, const GameMode& gm) const
{
    // WantCaptureMouse may remain set by hidden ImGui windows and can swallow
    // mining input in multiplayer. The actual menu state is the reliable gate.
    if (mc::menu::visible())
        return false;
    if (!gm.valid())
        return false;
    if (onlySurvival_)
    {
        // Some server sessions expose the base GameMode vtable even while the
        // player is in survival. Reject only the explicitly creative class;
        // treating the base class as non-survival made FastBreak local-only.
        const uintptr_t table = mem::read<uintptr_t>(gm.address());
        if (table == mem::resolve(vt::CreativeMode))
            return false;
    }

    if (requireClick_)
    {
        const bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (!lmb && !lp.leftClickFlag())
            return false;
    }
    return true;
}

void FastBreak::onFrame(float)
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer lp = game.localPlayer();
    if (!lp.valid())
        return;

    GameMode gm = game.gameMode();
    if (!canRun(lp, gm))
        return;

    // Solo aceleramos una rotura ya iniciada por el juego. Con progreso 0 no hay
    // bloque en curso sincronizado con el server (o el jugador no esta minando),
    // y forzar el campo solo provocaria un reinicio del ciclo y un paquete extra.
    const float progress = gm.destroyProgress();
    if (progress <= 0.0f)
    {
        if (debug_ && GetTickCount64() - lastDebugMs_ >= 1000)
        {
            lastDebugMs_ = GetTickCount64();
            MC_LOG("[FastBreak] waiting: gm=%p vt=%p progress=%.3f click=%d",
                   reinterpret_cast<void*>(gm.address()),
                   reinterpret_cast<void*>(mem::read<uintptr_t>(gm.address())),
                   progress,
                   ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0) || lp.leftClickFlag());
        }
        return;
    }

    BlockPos target;
    if (!gm.destroyTarget(target))
        return;

    // continueDestroyBlock compara la pos pedida con la que ya tiene en curso:
    // al pasarle la misma, no reinicia el estado ni reenvia el PlayerActionPacket
    // de inicio, solo acumula progreso y dispara destroyBlock al llegar a 1.0.
    const float forced = (mode_ == 0)
                             ? 1.0f
                             : std::min(1.0f, progress * std::max(1.0f, speed_));

    gm.setDestroyProgress(forced);
    gm.clearDestroyCooldown();
    gm.continueDestroyBlock(lp.address(), target, 1);

    if (debug_)
    {
        const uint64_t now = GetTickCount64();
        if (now - lastDebugMs_ >= 500 || !(target == lastTarget_))
        {
            lastDebugMs_ = now;
            lastTarget_ = target;
            MC_LOG("[FastBreak] pos=(%d,%d,%d) progress=%.3f forced=%.3f mode=%d",
                   target.x, target.y, target.z, progress, forced, mode_);
        }
    }
}

void FastBreak::drawSettings()
{
    const char* modes[] = {"Instant", "Multiplier"};
    widgets::CCombo("Mode##fb", &mode_, modes, 2);
    if (mode_ == 1)
        widgets::CSlider("Speed##fb", &speed_, 1.0f, 20.0f, "%.1fx");
    widgets::Checkbox("Require click##fb", &requireClick_);
    widgets::Checkbox("Only survival##fb", &onlySurvival_);
    widgets::Checkbox("Debug##fb", &debug_);
}

MC_REGISTER_MODULE(FastBreak);

}
