#include "RotationHelper.hpp"

#include "Features/Smoothing.hpp"
#include "Framework/Hook.hpp"
#include "Framework/Log.hpp"
#include "Framework/Math.hpp"
#include "Framework/Memory.hpp"
#include "SDK/Classes/LocalPlayer.hpp"
#include "SDK/Offsets.hpp"

#include <cmath>

namespace mc::rotation_helper {
namespace {

constexpr float kMoveThresholdSquared = 0.0025000002f;
constexpr float kMaxBodyHeadDifference = 75.0f;
constexpr float kRenderRotationResponse = 30.0f;

struct RotationState
{
    uintptr_t actor = 0;
    float targetYaw = 0.0f;
    float targetPitch = 0.0f;
    float bodyYaw = 0.0f;
    float displayedBodyYaw = 0.0f;
    // Head model yaw is relative to the torso on the Y axis. Keep that local
    // delta independent so body smoothing cannot swallow the head rotation.
    float displayedHeadYawY = 0.0f;
    float displayedHeadPitch = 0.0f;
    Vec3 lastPosition{};
    double lastUpdateMs = 0.0;
    int priority = 0;
    bool hasPosition = false;
    bool active = false;
};

RotationState g_state;
using PlayerRendererModelSetupFn = __int64(__fastcall*)(
    void*, void*, void*, void*, void*, void*, float);
void* g_playerRendererModelSetupOriginal = nullptr;
void* g_playerRendererModelSetupTarget = nullptr;
bool g_playerRendererModelSetupAttempted = false;

constexpr uintptr_t kRotationFieldOffsets[4] = {
    off::player::BodyYawField,
    off::player::BodyYawPreviousField,
    off::player::HeadYaw,
    off::player::HeadYawPreviousField,
};

uintptr_t fieldAddress(uintptr_t actor, int index)
{
    return actor + kRotationFieldOffsets[index];
}

float approachAngle(float current, float target, float dt)
{
    // Use a frame-rate independent response and the shortest angular path.
    // The response is slightly faster than Solstice's 20 Hz lerp to reduce
    // visible lag while retaining its per-frame smoothing behavior.
    const float alpha = 1.0f - std::exp(-kRenderRotationResponse * dt);
    return wrapDegrees(current + wrapDegrees(target - current) * alpha);
}

float bodyYawFor(float bodyYaw, float headYaw, const Vec3& position,
                 const Vec3& previousPosition, bool hasPosition)
{
    float bodyTarget = bodyYaw;
    const float dx = position.x - previousPosition.x;
    const float dz = position.z - previousPosition.z;
    if (hasPosition && dx * dx + dz * dz > kMoveThresholdSquared)
    {
        // Match Solstice's BaseTick body-yaw update: movement yaw stays in
        // atan2's native range while the head yaw is normalized first.
        const float movementYaw =
            std::atan2(dz, dx) * 180.0f / 3.14159265358979323846f - 90.0f;
        const float difference = std::fabs(wrapDegrees(headYaw) - movementYaw);
        bodyTarget = movementYaw;
        if (difference > 95.0f && difference < 265.0f)
            bodyTarget = movementYaw - 180.0f;
    }

    // Bedrock's body follows horizontal movement, then stays within 75 degrees
    // of the head. This is the same body/head separation used by Solstice.
    float turn = wrapDegrees(bodyTarget - bodyYaw);
    bodyYaw = wrapDegrees(bodyYaw + turn * 0.3f);
    float bodyHeadDifference = wrapDegrees(headYaw - bodyYaw);
    if (bodyHeadDifference < -kMaxBodyHeadDifference)
        bodyHeadDifference = -kMaxBodyHeadDifference;
    if (bodyHeadDifference >= kMaxBodyHeadDifference)
        bodyHeadDifference = kMaxBodyHeadDifference;
    bodyYaw = wrapDegrees(headYaw - bodyHeadDifference);
    if (bodyHeadDifference * bodyHeadDifference > 2500.0f)
        bodyYaw = wrapDegrees(bodyYaw + bodyHeadDifference * 0.2f);
    return bodyYaw;
}

bool readRotation(uintptr_t actor, float (&values)[4])
{
    for (int i = 0; i < 4; ++i)
    {
        const uintptr_t address = fieldAddress(actor, i);
        if (!mem::isReadable(address, sizeof(float)) ||
            !mem::rawRead(&values[i], address, sizeof(float)) ||
            !std::isfinite(values[i]))
            return false;
    }
    return true;
}

__int64 __fastcall playerRendererModelSetupDetour(
    void* renderer, void* arg1, void* arg2, void* actor,
    void* arg4, void* arg5, float partialTick)
{
    const auto original = reinterpret_cast<PlayerRendererModelSetupFn>(
        g_playerRendererModelSetupOriginal);
    if (!original)
        return 0;

    const uintptr_t actorAddress = reinterpret_cast<uintptr_t>(actor);
    float actorAngles[4]{};
    float actorPitch = 0.0f;
    float actorRenderPitch = 0.0f;
    const bool overrideModelAngles = g_state.active &&
        actorAddress == g_state.actor && actor &&
        mem::isReadable(actorAddress + off::player::BodyYawField,
            sizeof(actorAngles)) &&
        mem::isReadable(actorAddress + off::player::Pitch, sizeof(float)) &&
        mem::isReadable(actorAddress + off::player::RenderPitch, sizeof(float)) &&
        readRotation(actorAddress, actorAngles) &&
        mem::rawRead(&actorPitch, actorAddress + off::player::Pitch, sizeof(float)) &&
        mem::rawRead(&actorRenderPitch, actorAddress + off::player::RenderPitch,
            sizeof(float)) &&
        std::isfinite(actorPitch) && std::isfinite(actorRenderPitch) &&
        std::isfinite(g_state.displayedBodyYaw) &&
        std::isfinite(g_state.displayedHeadYawY) &&
        std::isfinite(g_state.displayedHeadPitch);

    if (!overrideModelAngles)
    {
        return original(renderer, arg1, arg2, actor, arg4, arg5, partialTick);
    }

    // The native routine reads body/head yaw and pitch from Actor, interpolates
    // pitch from +0x44 to +0x3C, then passes head yaw relative to body yaw and
    // the resulting pitch into the model. Keep all substitutions scoped to
    // this call so camera and persistent Actor state remain untouched.
    const float renderAngles[4] = {
        g_state.displayedBodyYaw,
        g_state.displayedBodyYaw,
        wrapDegrees(g_state.displayedBodyYaw + g_state.displayedHeadYawY),
        wrapDegrees(g_state.displayedBodyYaw + g_state.displayedHeadYawY),
    };
    if (!mem::rawWrite(actorAddress + off::player::BodyYawField,
            renderAngles, sizeof(renderAngles)))
    {
        return original(renderer, arg1, arg2, actor, arg4, arg5, partialTick);
    }

    const float renderPitch = g_state.displayedHeadPitch;
    if (!mem::rawWrite(actorAddress + off::player::Pitch,
            &renderPitch, sizeof(renderPitch)))
    {
        mem::rawWrite(actorAddress + off::player::BodyYawField,
            actorAngles, sizeof(actorAngles));
        return original(renderer, arg1, arg2, actor, arg4, arg5, partialTick);
    }
    if (!mem::rawWrite(actorAddress + off::player::RenderPitch,
            &renderPitch, sizeof(renderPitch)))
    {
        mem::rawWrite(actorAddress + off::player::Pitch,
            &actorPitch, sizeof(actorPitch));
        mem::rawWrite(actorAddress + off::player::BodyYawField,
            actorAngles, sizeof(actorAngles));
        return original(renderer, arg1, arg2, actor, arg4, arg5, partialTick);
    }
    __int64 result = 0;
    __try
    {
        result = original(renderer, arg1, arg2, actor, arg4, arg5, partialTick);
    }
    __finally
    {
        // Restore every actor rotation field even if model setup exits through
        // an exception. The camera's pitch/yaw remain the saved values.
        mem::rawWrite(actorAddress + off::player::RenderPitch,
            &actorRenderPitch, sizeof(actorRenderPitch));
        mem::rawWrite(actorAddress + off::player::Pitch,
            &actorPitch, sizeof(actorPitch));
        mem::rawWrite(actorAddress + off::player::BodyYawField,
            actorAngles, sizeof(actorAngles));
    }
    return result;
}

bool installPlayerRendererModelSetupHook()
{
    if (g_playerRendererModelSetupTarget)
        return true;
    if (g_playerRendererModelSetupAttempted)
        return false;
    g_playerRendererModelSetupAttempted = true;

    void* target = reinterpret_cast<void*>(
        mem::resolve(off::fn::PlayerRendererModelSetup));
    if (!target || !mem::isReadable(reinterpret_cast<uintptr_t>(target), 16) ||
        !hook::create(target,
            reinterpret_cast<void*>(&playerRendererModelSetupDetour),
            &g_playerRendererModelSetupOriginal))
    {
        MC_LOG_ERROR("[Rotation] PlayerRenderer model hook install failed");
        return false;
    }

    g_playerRendererModelSetupTarget = target;
    MC_LOG("[Rotation] render-scoped yaw hook active @ %p", target);
    return true;
}

void clearState()
{
    // Only render-state output is overridden; the actor's real yaw fields stay
    // untouched, so disabling the feature has no actor-state cleanup phase.
    g_state = {};
}

} // namespace

bool requestClientRotation(const LocalPlayer& player, float yaw, float pitch,
                           int priority)
{
    if (!player.valid() || !player.isLocal() || !std::isfinite(yaw) ||
        !std::isfinite(pitch))
        return false;

    const uintptr_t actor = player.address();
    if (g_state.active && priority < g_state.priority)
        return false;
    if (g_state.active && g_state.actor != actor)
        clearState();

    float current[4]{};
    if (!readRotation(actor, current))
    {
        clearState();
        return false;
    }

    if (!g_state.active)
    {
        g_state.actor = actor;
        g_state.bodyYaw = current[0];
        g_state.displayedBodyYaw = current[0];
        g_state.displayedHeadYawY = wrapDegrees(current[2] - current[0]);
        g_state.displayedHeadPitch = clampf(player.pitch(), -90.0f, 90.0f);
        g_state.targetYaw = wrapDegrees(yaw);
        g_state.targetPitch = clampf(pitch, -90.0f, 90.0f);
        g_state.lastPosition = player.posExact();
        g_state.lastUpdateMs = smooth::detail::nowMs();
        g_state.hasPosition = true;
        g_state.priority = priority;
        g_state.active = true;
    }
    else
    {
        g_state.priority = priority;
        g_state.targetYaw = wrapDegrees(yaw);
        g_state.targetPitch = clampf(pitch, -90.0f, 90.0f);
    }

    return true;
}

void tickClientRotation(const LocalPlayer& player)
{
    if (!g_state.active || !player.valid() || !player.isLocal() ||
        player.address() != g_state.actor)
        return;

    const Vec3 position = player.posExact();
    g_state.bodyYaw = bodyYawFor(g_state.bodyYaw, g_state.targetYaw,
        position, g_state.lastPosition, g_state.hasPosition);
    g_state.lastPosition = position;
    g_state.hasPosition = true;
}

void updateClientRotation()
{
    if (!g_state.active)
        return;

    LocalPlayer player(g_state.actor);
    if (!player.valid() || !player.isLocal())
    {
        clearState();
        return;
    }

    installPlayerRendererModelSetupHook();
    tickClientRotation(player);

    const double now = smooth::detail::nowMs();
    double dtMs = now - g_state.lastUpdateMs;
    if (dtMs < 0.0)
        dtMs = 0.0;
    if (dtMs > 100.0)
        dtMs = 100.0;

    const float dt = static_cast<float>(dtMs / 1000.0);
    g_state.displayedBodyYaw = approachAngle(g_state.displayedBodyYaw,
        g_state.bodyYaw, dt);
    const float targetHeadYawY =
        wrapDegrees(g_state.targetYaw - g_state.displayedBodyYaw);
    g_state.displayedHeadYawY = approachAngle(g_state.displayedHeadYawY,
        targetHeadYawY, dt);
    const float pitchAlpha = 1.0f - std::exp(-kRenderRotationResponse * dt);
    g_state.displayedHeadPitch +=
        (g_state.targetPitch - g_state.displayedHeadPitch) * pitchAlpha;
    g_state.displayedHeadPitch = clampf(g_state.displayedHeadPitch, -90.0f, 90.0f);
    g_state.lastUpdateMs = now;
}

bool outgoingHeadYaw(float& yaw)
{
    if (!g_state.active)
        return false;

    LocalPlayer player(g_state.actor);
    if (!player.valid() || !player.isLocal())
        return false;

    yaw = player.yaw();
    return std::isfinite(yaw);
}

void clearClientRotation()
{
    clearState();
}

void clearClientRotation(int priority)
{
    if (g_state.active && g_state.priority == priority)
        clearState();
}

} // namespace mc::rotation_helper
