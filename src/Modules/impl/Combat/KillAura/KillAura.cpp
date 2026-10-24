#include "KillAura.hpp"

#include "Features/Friends.hpp"
#include "Features/Raycast.hpp"
#include "Features/Rotation.hpp"
#include "Features/RotationHelper.hpp"
#include "Framework/Log.hpp"
#include "Framework/Math.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Widgets.hpp"
#include "SDK/Game.hpp"
#include "SDK/Offsets.hpp"

#include <imgui.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>

namespace mc {
namespace {

constexpr float kMinDistance = 0.1f;
constexpr float kTargetEyeHeight = 0.85f;
constexpr unsigned kRotationLifetimeMs = 120;
constexpr unsigned kFlickRotationLifetimeMs = 180;

struct TargetCandidate
{
    Actor actor;
    float distance = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
};

Vec3 targetPoint(const Actor& actor)
{
    Vec3 point = actor.posExact();
    point.y = actor.pos().y + actor.height() * kTargetEyeHeight;
    return point;
}

bool hasLineOfSight(const Vec3& eye, const Vec3& point, float distance)
{
    const BlockSource blocks = Game::get().blockSource();
    if (!blocks.valid())
        return false;

    const Vec3 direction{point.x - eye.x, point.y - eye.y, point.z - eye.z};
    const RaycastHit hit = raycastBlock(blocks, eye, direction, distance);
    return !hit.hit || hit.distance + 0.05f >= distance;
}

float horizontalSpeed(const Actor& actor)
{
    const Vec3 velocity = actor.velocity();
    return std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
}

} // namespace

KillAura::KillAura()
    : Module("KillAura", "Automatically attacks nearby players with silent rotations",
             Category::Combat, 0)
{
    markHasSettings();
}

bool KillAura::selectTarget(const Actor& local, Actor& target,
                            float& yaw, float& pitch) const
{
    if (!local.valid() || !local.isLocal())
        return false;

    Vec3 eye = local.posExact();
    eye.y = local.pos().y + off::gameconst::EyeHeight;
    const float viewYaw = local.yaw();
    const float viewPitch = local.pitch();
    const float speed = horizontalSpeed(local);
    const float effectiveRange = dynamicRange_ && speed < 0.05f
        ? dynamicRangeValue_
        : range_;
    std::vector<TargetCandidate> candidates;

    for (const Actor& candidate : Game::get().remotePlayersSnapshot())
    {
        if (!candidate.valid() || candidate.address() == local.address() ||
            candidate.health() <= 0)
            continue;

        const std::string name = candidate.name();
        if (name.empty() || friends::isFriend(name))
            continue;

        const Vec3 point = targetPoint(candidate);
        const float distance = distance3D(eye, point);
        if (!std::isfinite(distance) || distance < kMinDistance || distance > effectiveRange)
            continue;

        if (!attackThroughWalls_ && !hasLineOfSight(eye, point, distance))
            continue;

        const AimAngles angles = calcAngles(eye, point);
        if (std::fabs(angleDiff(viewYaw, angles.yaw)) > fov_ * 0.5f ||
            std::fabs(angles.pitch - viewPitch) > fov_ * 0.5f)
            continue;

        candidates.push_back({candidate, distance, angles.yaw, angles.pitch});
    }

    if (candidates.empty())
        return false;

    std::sort(candidates.begin(), candidates.end(),
        [](const TargetCandidate& a, const TargetCandidate& b) {
            return a.distance < b.distance;
        });

    size_t selected = 0;
    if (targetMode_ == 1) // Multi: cycle through all eligible targets.
    {
        selected = static_cast<size_t>(multiTargetCursor_ % candidates.size());
    }
    else if (targetMode_ == 2) // Switch: prefer the target attacked least recently.
    {
        auto lastAttackFor = [this](uintptr_t address) {
            const auto it = lastTargetAttacks_.find(address);
            return it == lastTargetAttacks_.end()
                ? std::chrono::steady_clock::time_point::min()
                : it->second;
        };
        for (size_t i = 1; i < candidates.size(); ++i)
        {
            const auto candidateTime = lastAttackFor(candidates[i].actor.address());
            const auto selectedTime = lastAttackFor(candidates[selected].actor.address());
            if (candidateTime < selectedTime)
                selected = i;
        }
    }

    target = candidates[selected].actor;
    yaw = candidates[selected].yaw;
    pitch = candidates[selected].pitch;
    return true;
}

void KillAura::clearTarget()
{
    rot::clear(rot::PrioCombat);
    rotation_helper::clearClientRotation(rot::PrioCombat);
}

void KillAura::onEnable()
{
    lastDimension_ = Game::get().levelAddr();
    lastAttack_ = {};
    lastSwing_ = {};
    multiTargetCursor_ = 0;
    lastTargetAttacks_.clear();
    nextAttackDelayMs_ = 1000.0f / clampf(cps_, 1.0f, 30.0f);
}

void KillAura::onFrame(float /*dt*/)
{
    if (!Game::get().localFound() || menu::visible() || ImGui::GetIO().WantCaptureMouse)
    {
        clearTarget();
        return;
    }

    LocalPlayer local = Game::get().localPlayer();
    Actor target;
    float yaw = 0.0f;
    float pitch = 0.0f;
    if (!selectTarget(local, target, yaw, pitch))
    {
        clearTarget();
        return;
    }

    if (rotateMode_ == 0)
    {
        clearTarget();
        return;
    }

    rotation_helper::requestClientRotation(local, yaw, pitch, rot::PrioCombat);
    if (rotateMode_ == 1)
        rot::request(pitch, yaw, kRotationLifetimeMs, rot::PrioCombat,
                     headYaw_ ? 90.0f : 0.0f);
}

void KillAura::onTick()
{
    if (!Game::get().localFound() || menu::visible() || ImGui::GetIO().WantCaptureMouse)
    {
        clearTarget();
        return;
    }

    const uintptr_t dimension = Game::get().levelAddr();
    if (dimension && !lastDimension_)
        lastDimension_ = dimension;
    else if (dimension && lastDimension_ && dimension != lastDimension_)
    {
        lastDimension_ = dimension;
        if (autoDisableOnDimensionChange_)
        {
            MC_LOG("[KillAura] disabled after dimension change");
            setEnabled(false);
            return;
        }
    }

    LocalPlayer local = Game::get().localPlayer();
    Actor target;
    float yaw = 0.0f;
    float pitch = 0.0f;
    if (!selectTarget(local, target, yaw, pitch))
    {
        clearTarget();
        return;
    }

    // Refresh rotations here too in case this module ticks before its first
    // frame callback after enable.
    if (rotateMode_ == 0)
    {
        clearTarget();
    }
    else
    {
        rotation_helper::requestClientRotation(local, yaw, pitch, rot::PrioCombat);
        if (rotateMode_ == 1)
            rot::request(pitch, yaw, kRotationLifetimeMs, rot::PrioCombat,
                         headYaw_ ? 90.0f : 0.0f);
    }

    const auto now = std::chrono::steady_clock::now();
    const float delayMs = randomizeAps_
        ? nextAttackDelayMs_
        : 1000.0f / clampf(cps_, 1.0f, 30.0f);
    const auto delay = std::chrono::duration<float, std::milli>(delayMs);
    if (lastAttack_.time_since_epoch().count() != 0 && now - lastAttack_ < delay)
        return;

    // Flick rotates the next outgoing movement packet only when an attack is
    // ready; Normal keeps refreshing the silent rotation while a target exists.
    if (rotateMode_ == 2)
        rot::request(pitch, yaw, kFlickRotationLifetimeMs, rot::PrioCombat,
                     headYaw_ ? 90.0f : 0.0f);

    if (swing_ &&
        (!swingDelay_ || lastSwing_.time_since_epoch().count() == 0 ||
         now - lastSwing_ >= std::chrono::duration<float>(swingDelaySeconds_)))
    {
        (void)local.swing();
        lastSwing_ = now;
    }

    const bool sent = Game::get().gameMode().attack(local, target);
    if (debug_)
    {
        static auto lastLog = std::chrono::steady_clock::time_point{};
        if (lastLog.time_since_epoch().count() == 0 ||
            now - lastLog >= std::chrono::seconds(1))
        {
            MC_LOG("[KillAura] target=%s distance=%.2f packet-call=%s",
                   target.name().c_str(), distance3D(local.eyePos(), targetPoint(target)),
                   sent ? "ok" : "failed");
            lastLog = now;
        }
    }

    lastAttack_ = now;
    lastTargetAttacks_[target.address()] = now;
    if (targetMode_ == 1)
        ++multiTargetCursor_;

    if (randomizeAps_)
    {
        static thread_local std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> apsDistribution(
            clampf(apsMin_, 1.0f, 30.0f), clampf(apsMax_, 1.0f, 30.0f));
        const float aps = apsDistribution(rng);
        nextAttackDelayMs_ = 1000.0f / clampf(aps, 1.0f, 30.0f);
    }
}

void KillAura::onDisable()
{
    clearTarget();
    lastDimension_ = 0;
    multiTargetCursor_ = 0;
    lastTargetAttacks_.clear();
}

std::string KillAura::suffix() const
{
    char buf[32];
    const char* modes[] = {"Single", "Multi", "Switch"};
    std::snprintf(buf, sizeof(buf), "%.1fm %.0fAPS %s", range_,
                  randomizeAps_ ? (apsMin_ + apsMax_) * 0.5f : cps_,
                  modes[targetMode_]);
    return buf;
}

nlohmann::json KillAura::saveSettings() const
{
    return nlohmann::json{
        {"range", range_},
        {"fov", fov_},
        {"cps", cps_},
        {"targetMode", targetMode_},
        {"rotateMode", rotateMode_},
        {"headYaw", headYaw_},
        {"dynamicRange", dynamicRange_},
        {"dynamicRangeValue", dynamicRangeValue_},
        {"randomizeAps", randomizeAps_},
        {"apsMin", apsMin_},
        {"apsMax", apsMax_},
        {"swing", swing_},
        {"swingDelay", swingDelay_},
        {"swingDelaySeconds", swingDelaySeconds_},
        {"attackThroughWalls", attackThroughWalls_},
        {"autoDisableOnDimensionChange", autoDisableOnDimensionChange_},
        {"debug", debug_},
    };
}

void KillAura::loadSettings(const nlohmann::json& j)
{
    range_ = clampf(j.value("range", range_), 1.0f, 8.0f);
    fov_ = clampf(j.value("fov", fov_), 0.0f, 360.0f);
    cps_ = clampf(j.value("cps", cps_), 1.0f, 30.0f);
    targetMode_ = std::clamp(j.value("targetMode", targetMode_), 0, 2);
    rotateMode_ = std::clamp(j.value("rotateMode", rotateMode_), 0, 2);
    headYaw_ = j.value("headYaw", headYaw_);
    dynamicRange_ = j.value("dynamicRange", dynamicRange_);
    dynamicRangeValue_ = clampf(j.value("dynamicRangeValue", dynamicRangeValue_), 1.0f, 8.0f);
    randomizeAps_ = j.value("randomizeAps", randomizeAps_);
    apsMin_ = clampf(j.value("apsMin", apsMin_), 1.0f, 30.0f);
    apsMax_ = clampf(j.value("apsMax", apsMax_), apsMin_, 30.0f);
    swing_ = j.value("swing", swing_);
    swingDelay_ = j.value("swingDelay", swingDelay_);
    swingDelaySeconds_ = clampf(j.value("swingDelaySeconds", swingDelaySeconds_), 0.0f, 10.0f);
    attackThroughWalls_ = j.value("attackThroughWalls", attackThroughWalls_);
    autoDisableOnDimensionChange_ = j.value("autoDisableOnDimensionChange", autoDisableOnDimensionChange_);
    debug_ = j.value("debug", debug_);
    nextAttackDelayMs_ = 1000.0f / cps_;
}

void KillAura::drawSettings()
{
    static const char* targetModes[] = {"Single", "Multi", "Switch"};
    static const char* rotateModes[] = {"None", "Normal", "Flick"};
    widgets::CCombo("Mode##ka", &targetMode_, targetModes, 3);
    widgets::CCombo("Rotate Mode##ka", &rotateMode_, rotateModes, 3);
    widgets::CSlider("Range##ka", &range_, 1.0f, 8.0f, "%.1f");
    widgets::CSlider("FOV##ka", &fov_, 0.0f, 360.0f, "%.0f");
    widgets::Checkbox("Head Yaw##ka", &headYaw_);
    widgets::Checkbox("Dynamic Range##ka", &dynamicRange_);
    if (dynamicRange_)
        widgets::CSlider("Dynamic Value##ka", &dynamicRangeValue_, 1.0f, 8.0f, "%.1f");

    widgets::Checkbox("Randomize APS##ka", &randomizeAps_);
    if (randomizeAps_)
    {
        widgets::CSlider("APS Min##ka", &apsMin_, 1.0f, 30.0f, "%.1f");
        widgets::CSlider("APS Max##ka", &apsMax_, apsMin_, 30.0f, "%.1f");
    }
    else
        widgets::CSlider("APS##ka", &cps_, 1.0f, 30.0f, "%.1f");

    widgets::Checkbox("Swing##ka", &swing_);
    if (swing_)
    {
        widgets::Checkbox("Swing Delay##ka", &swingDelay_);
        if (swingDelay_)
            widgets::CSlider("Swing Delay Value##ka", &swingDelaySeconds_, 0.0f, 10.0f, "%.2f s");
    }
    widgets::Checkbox("Attack Through Walls##ka", &attackThroughWalls_);
    widgets::Checkbox("Auto Disable on Dimension Change##ka", &autoDisableOnDimensionChange_);
    widgets::Checkbox("Debug##ka", &debug_);
}

MC_REGISTER_MODULE(KillAura);

}
