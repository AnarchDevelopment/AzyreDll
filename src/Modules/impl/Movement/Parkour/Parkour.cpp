#include "Parkour.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "GUI/Menu.hpp"
#include "SDK/World/BlockSource/BlockSource.hpp"
#include "SDK/Entity/LocalPlayer.hpp"
#include "SDK/Core/Game.hpp"
#include "SDK/Engine/Memory/Offsets.hpp"

#include <windows.h>
#include <cmath>

namespace mc {

namespace {

bool keyDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
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

Parkour::Parkour()
    : Module("Parkour", "Automatically jumps when reaching the edge of a block.", Category::Movement, 0)
{
    markHasSettings();
}

void Parkour::onEnable()
{
    lastJumpMs_ = 0;
}

void Parkour::onTick()
{
    if (menu::visible())
        return;

    Game& game = Game::get();
    if (!game.localFound())
        return;

    const Client client = game.client();
    if (client.chatOpen())
        return;

    LocalPlayer local = game.localPlayer();
    if (!local.valid())
        return;

    BlockSource bs = game.blockSource();
    if (!bs.valid())
        return;

    if (checkSneak_ && keyDown(VK_SHIFT))
        return;

    const ULONGLONG now = GetTickCount64();
    if (!local.onGround())
        return;

    Vec3 feet = local.pos();       // AABB min: feet Y (+0xB4)
    Vec3 exact = local.posExact(); // mPosition (+0x0C): horizontal center
    Vec3 vel = local.velocity();

    bool sane = std::isfinite(feet.x) && std::isfinite(feet.y) && std::isfinite(feet.z) &&
                std::isfinite(exact.x) && std::isfinite(exact.z) &&
                std::isfinite(vel.x) && std::isfinite(vel.y) && std::isfinite(vel.z);
    if (!sane)
        return;

    // A short debounce prevents re-triggering while the ground flag lags one
    // tick behind the upward velocity. Landing is detected from the native
    // ground flag and vertical speed, so a missed airborne frame cannot leave
    // Parkour permanently disarmed.
    if (vel.y > 0.08f || (lastJumpMs_ != 0 && now - lastJumpMs_ < 120))
        return;

    // Movement direction determination:
    float fwd = 0.0f;
    float str = 0.0f;
    if (keyDown('W')) fwd += 1.0f;
    if (keyDown('S')) fwd -= 1.0f;
    if (keyDown('D')) str += 1.0f;
    if (keyDown('A')) str -= 1.0f;

    float hSpeed = vecLength2D(vel);
    bool hasInput = (std::abs(fwd) > 0.01f || std::abs(str) > 0.01f);
    if (!hasInput && hSpeed < 0.05f)
        return;

    Vec3 dir;
    if (hasInput)
        dir = wishDirFromYaw(local.yaw(), fwd, str);
    else
        dir = {vel.x / hSpeed, 0.0f, vel.z / hSpeed};

    if (vecLength2D(dir) < 0.01f)
        return;

    int groundY = static_cast<int>(std::floor(feet.y - 0.1f));

    // Player bounding box half-width is ~0.3 blocks.
    // Probe ahead along the movement vector:
    float probeDist = 0.3f + edgeDistance_;
    Vec3 probePos = exact + (dir * probeDist);

    int px = static_cast<int>(std::floor(probePos.x));
    int pz = static_cast<int>(std::floor(probePos.z));

    // Block right under the forward probe point at current ground height
    int groundBelowProbe = bs.getBlockId(px, groundY, pz);

    // If ground ahead is still solid, player is not at the edge yet
    if (isSolidId(groundBelowProbe))
        return;

    // Ensure we are not just running into a solid wall
    int wallAtFeet = bs.getBlockId(px, groundY + 1, pz);
    if (isSolidId(wallAtFeet))
        return;

    // Optional check: ensure there is an actual fall/drop (air or deep gap)
    if (checkFallDepth_)
    {
        int blockOneDown = bs.getBlockId(px, groundY - 1, pz);
        if (isSolidId(blockOneDown))
            return; // Only a 1-block shallow step, not a gap
    }

    // Verify support under the player's footprint. Checking only the center
    // and one AABB corner loses support on diagonal edges and can prevent
    // later jumps after landing off-center.
    float width = off::gameconst::PlayerWidth;
    float actorWidth = 0.0f;
    if (mem::rawRead(&actorWidth, local.address() + off::player::AabbWidth,
                     sizeof(actorWidth)) &&
        std::isfinite(actorWidth) && actorWidth >= 0.1f && actorWidth <= 2.5f)
        width = actorWidth;
    const float half = width * 0.45f;
    bool hasSupport = false;
    for (int sx : {-1, 1})
    {
        for (int sz : {-1, 1})
        {
            const int supportId = bs.getBlockId(
                static_cast<int>(std::floor(exact.x + sx * half)), groundY,
                static_cast<int>(std::floor(exact.z + sz * half)));
            hasSupport = hasSupport || isSolidId(supportId);
        }
    }
    if (!hasSupport)
        return;

    // At the edge: jump with forward sprint momentum
    Vec3 v = local.velocity();
    v.y = jumpHeight_;

    if (boost_ > 0.0f && vecLength2D(dir) > 0.01f)
    {
        if (preserveSpeed_ && hSpeed > 0.05f)
        {
            float targetSpeed = hSpeed + boost_;
            v.x = dir.x * targetSpeed;
            v.z = dir.z * targetSpeed;
        }
        else
        {
            v.x += dir.x * boost_;
            v.z += dir.z * boost_;
        }
    }

    local.setVelocity(v);
    // Do NOT call local.setOnGround(false) — the game's native collision resolution
    // smoothly transitions the player to airborne without canceling forward motion.
    lastJumpMs_ = now;
}

void Parkour::drawSettings()
{
    widgets::CSlider("Edge Distance##pk", &edgeDistance_, 0.00f, 0.30f, "%.2f");
    widgets::CSlider("Jump Height##pk", &jumpHeight_, 0.20f, 0.80f, "%.2f");
    widgets::CSlider("Jump Boost##pk", &boost_, 0.00f, 0.50f, "%.2f");
    widgets::Checkbox("Preserve Speed##pk", &preserveSpeed_);
    widgets::Checkbox("Check Sneak##pk", &checkSneak_);
    widgets::Checkbox("Check Fall Depth##pk", &checkFallDepth_);
}

nlohmann::json Parkour::saveSettings() const
{
    return {
        {"edgeDistance", edgeDistance_},
        {"jumpHeight", jumpHeight_},
        {"boost", boost_},
        {"preserveSpeed", preserveSpeed_},
        {"checkSneak", checkSneak_},
        {"checkFallDepth", checkFallDepth_},
    };
}

void Parkour::loadSettings(const nlohmann::json& j)
{
    edgeDistance_ = clampf(j.value("edgeDistance", edgeDistance_), 0.00f, 0.30f);
    jumpHeight_ = clampf(j.value("jumpHeight", jumpHeight_), 0.20f, 0.80f);
    boost_ = clampf(j.value("boost", boost_), 0.00f, 0.50f);
    preserveSpeed_ = j.value("preserveSpeed", preserveSpeed_);
    checkSneak_ = j.value("checkSneak", checkSneak_);
    checkFallDepth_ = j.value("checkFallDepth", checkFallDepth_);
}

MC_REGISTER_MODULE(Parkour);

} // namespace mc
