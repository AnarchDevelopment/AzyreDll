#include "FreeLook.hpp"

#include "Features/Rotation.hpp"
#include "Features/RotationHelper.hpp"
#include "SDK/Game.hpp"

#include <cmath>

namespace mc {

namespace {
constexpr unsigned kRotationRefreshMs = 500;
constexpr int kFreeLookPriority = rot::PrioVisual;
}

FreeLook::FreeLook()
    : Module("FreeLook", "Look around while keeping the player facing unchanged",
             Category::Visuals, 0)
{
}

void FreeLook::onEnable()
{
    actor_ = 0;
    savedYaw_ = 0.0f;
    savedPitch_ = 0.0f;
    captured_ = false;
}

void FreeLook::onFrame(float /*dt*/)
{
    Game& game = Game::get();
    LocalPlayer player = game.localPlayer();
    if (!player.valid() || !player.isLocal())
        return;

    const uintptr_t currentActor = player.address();
    if (!captured_ || actor_ != currentActor)
    {
        // A respawn can replace LocalPlayer while the module is active. Drop
        // the old requests and capture the new actor's camera orientation.
        if (captured_)
        {
            rot::clear(kFreeLookPriority);
            rotation_helper::clearClientRotation(kFreeLookPriority);
        }

        actor_ = currentActor;
        savedYaw_ = player.yaw();
        savedPitch_ = player.pitch();
        captured_ = std::isfinite(savedYaw_) && std::isfinite(savedPitch_);
        if (!captured_)
            return;
    }

    // Actor yaw/pitch remain available to the game as camera input. The
    // packet hook and render-scoped model helper hold the orientation that
    // was active when FreeLook started.
    rot::request(savedPitch_, savedYaw_, kRotationRefreshMs,
                 kFreeLookPriority);
    rotation_helper::requestClientRotation(
        player, savedYaw_, savedPitch_, kFreeLookPriority);
}

void FreeLook::clearRotation(bool restoreCamera)
{
    if (restoreCamera && captured_ && actor_ &&
        Game::get().localAddr() == actor_)
    {
        LocalPlayer player(actor_);
        if (player.valid() && player.isLocal())
        {
            // FreeLook release returns the camera to the body direction that
            // was captured at activation, avoiding a body snap to the last
            // free-look angle.
            player.setYaw(savedYaw_);
            player.setPitch(savedPitch_);
        }
    }

    rot::clear(kFreeLookPriority);
    rotation_helper::clearClientRotation(kFreeLookPriority);
    actor_ = 0;
    captured_ = false;
}

void FreeLook::onDisable()
{
    clearRotation(true);
}

void FreeLook::onShutdown()
{
    clearRotation(true);
}

MC_REGISTER_MODULE(FreeLook);

}
