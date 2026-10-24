#include "Animations.hpp"

#include "Framework/Hook.hpp"
#include "Framework/Log.hpp"
#include "Framework/Memory.hpp"
#include "Features/Smoothing.hpp"
#include "GUI/Widgets.hpp"
#include "SDK/Classes/LocalPlayer.hpp"
#include "SDK/Game.hpp"
#include "SDK/Offsets.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iterator>
#include <windows.h>
#include <intrin.h>

namespace mc {
namespace {

using GetSwingDurationFn = int(__fastcall*)(void* actor);
GetSwingDurationFn g_originalGetSwingDuration = nullptr;
void* g_swingDurationTarget = nullptr;
std::atomic<bool> g_active{false};
std::atomic<int> g_swingTime{6};
using HumanoidModelAnimationSetupFn = __int64(__fastcall*)(void* model, void* actor);
HumanoidModelAnimationSetupFn g_originalModelAnimationSetup = nullptr;
void* g_modelAnimationSetupTarget = nullptr;
using ItemInHandDrawFn = __int64(__fastcall*)(void* renderer, void* actor,
    void* renderState, uint8_t hand, float partialTick);
using MatrixStackPushFn = void*(__fastcall*)(void* stack, uintptr_t out[2]);
using MatrixTranslateFn = void*(__fastcall*)(void* matrix, float x, float y, float z);
using MatrixRotateFn = void*(__fastcall*)(void* matrix, float radians,
    float axisX, float axisY, float axisZ);
ItemInHandDrawFn g_originalItemInHandDraw = nullptr;
void* g_itemInHandDrawTarget = nullptr;
MatrixStackPushFn g_matrixStackPush = nullptr;
MatrixTranslateFn g_matrixTranslate = nullptr;
MatrixRotateFn g_matrixRotate = nullptr;
std::atomic<bool> g_smallItemsActive{false};
std::atomic<bool> g_customSwingAngleActive{false};
std::atomic<bool> g_onlyOnBlockSwingAngle{false};
std::atomic<float> g_swingAngleDegrees{-31.0f};
std::atomic<bool> g_blockPoseActive{false};
std::atomic<bool> g_modelSetupHookAvailable{false};
std::atomic<bool> g_onlyOnBlock{true};
std::atomic<float> g_blockRotations[3]{-57.0f, -45.16f, 13.14f};
uintptr_t g_blendActor = 0;
uintptr_t g_blendModel = 0;
float g_blendedArmRotation[3]{};
double g_lastBlendTimeMs = 0.0;
bool g_blendInitialized = false;

int __fastcall getSwingDurationDetour(void* actor)
{
    const auto original = g_originalGetSwingDuration;
    if (!original)
        return g_swingTime.load(std::memory_order_relaxed);

    const int originalDuration = original(actor);
    if (!g_active.load(std::memory_order_relaxed))
        return originalDuration;

    if (!actor || reinterpret_cast<uintptr_t>(actor) != Game::get().localAddr())
        return originalDuration;

    return g_swingTime.load(std::memory_order_relaxed);
}

bool installSwingDurationHook()
{
    if (g_swingDurationTarget)
        return true;

    const uintptr_t address = mem::resolve(off::fn::ActorGetSwingDuration);
    if (!address || !mem::isReadable(address, 16))
    {
        MC_LOG_ERROR("[Animations] Swing duration function is not readable");
        return false;
    }

    void* target = reinterpret_cast<void*>(address);
    if (!hook::create(target,
            reinterpret_cast<void*>(&getSwingDurationDetour),
            reinterpret_cast<void**>(&g_originalGetSwingDuration)))
    {
        MC_LOG_ERROR("[Animations] Failed to install swing duration hook");
        return false;
    }

    g_swingDurationTarget = target;
    MC_LOG("[Animations] Swing duration hook installed @ %p", target);
    return true;
}

bool blockPoseRequested(uintptr_t actor)
{
    if (!g_blockPoseActive.load(std::memory_order_relaxed))
        return false;
    if (!g_onlyOnBlock.load(std::memory_order_relaxed))
        return true;
    if ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) == 0)
        return false;

    const ItemInstance held = LocalPlayer(actor).heldItem();
    const uintptr_t item = held.valid() ? held.item() : 0;
    return item && mem::isReadable(item, sizeof(uintptr_t));
}

bool isHoldingItemAndBlocking(uintptr_t actor)
{
    if (!actor || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) == 0)
        return false;
    const ItemInstance held = LocalPlayer(actor).heldItem();
    const uintptr_t item = held.valid() ? held.item() : 0;
    return item && mem::isReadable(item, sizeof(uintptr_t));
}

struct MatrixStackScope
{
    uintptr_t owner = 0;
    uintptr_t matrix = 0;
    bool active = false;

    bool push()
    {
        const uintptr_t stackAddress = mem::resolve(off::render::MatrixStack);
        if (!stackAddress || !g_matrixStackPush || !mem::isReadable(stackAddress, 0x18))
            return false;

        uintptr_t out[2]{};
        g_matrixStackPush(reinterpret_cast<void*>(stackAddress), out);
        owner = out[0];
        matrix = out[1];
        if (!owner || !matrix)
            return false;
        active = true;
        if (!mem::isReadable(matrix, 64) ||
            !mem::isReadable(owner + sizeof(uintptr_t), 2 * sizeof(uintptr_t)))
        {
            pop();
            return false;
        }
        return true;
    }

    void pop()
    {
        if (!active)
            return;

        uintptr_t currentTop = 0;
        const uintptr_t cursorAddress = owner + sizeof(uintptr_t);
        mem::rawRead(&currentTop, cursorAddress, sizeof(currentTop));
        // sub_140793F40 returns the pre-push cursor as `matrix`. Restore that
        // exact cursor after the item draw so nested renderer frames cannot
        // leak the temporary hand transform into later HUD/GUI rendering.
        if (!mem::rawWrite(cursorAddress, &matrix, sizeof(matrix)))
            MC_LOG("[Animations] Failed to restore render matrix stack cursor");
        else if (currentTop != matrix + 64)
            MC_LOG("[Animations] Restored render matrix stack after nested draw frame");
        active = false;
    }

    ~MatrixStackScope() { pop(); }
};

float interpolatedSwingProgress(uintptr_t actor, float partialTick)
{
    constexpr uintptr_t previousProgressOffset = 0xDE8;
    constexpr uintptr_t currentProgressOffset = 0xDEC;
    float previous = 0.0f;
    float current = 0.0f;
    if (!mem::rawRead(&previous, actor + previousProgressOffset, sizeof(previous)) ||
        !mem::rawRead(&current, actor + currentProgressOffset, sizeof(current)) ||
        !std::isfinite(previous) || !std::isfinite(current))
        return 0.0f;

    float delta = current - previous;
    if (delta < 0.0f)
        delta += 1.0f;
    return std::clamp(previous + delta * std::clamp(partialTick, 0.0f, 1.0f), 0.0f, 1.0f);
}

__int64 __fastcall itemInHandDrawDetour(void* renderer, void* actor,
    void* renderState, uint8_t hand, float partialTick)
{
    const auto original = g_originalItemInHandDraw;
    if (!original)
        return 0;

    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uintptr_t firstPersonCallReturn = mem::resolve(off::fn::ItemInHandDrawReturn);
    const uintptr_t actorAddress = reinterpret_cast<uintptr_t>(actor);
    const bool firstPersonLocalDraw = caller == firstPersonCallReturn && actorAddress &&
        actorAddress == Game::get().localAddr();

    if (!firstPersonLocalDraw)
        return original(renderer, actor, renderState, hand, partialTick);

    const bool smallItems = g_smallItemsActive.load(std::memory_order_relaxed);
    const bool customAngle = g_customSwingAngleActive.load(std::memory_order_relaxed) &&
        (!g_onlyOnBlockSwingAngle.load(std::memory_order_relaxed) ||
            isHoldingItemAndBlocking(actorAddress));
    if ((!smallItems && !customAngle) || !g_matrixStackPush)
        return original(renderer, actor, renderState, hand, partialTick);

    MatrixStackScope matrixScope;
    if (!matrixScope.push())
        return original(renderer, actor, renderState, hand, partialTick);

    if (smallItems && g_matrixTranslate)
    {
        g_matrixTranslate(reinterpret_cast<void*>(matrixScope.matrix), 0.5f, -0.2f, -0.6f);
    }

    if (customAngle && g_matrixRotate)
    {
        const float swingProgress = interpolatedSwingProgress(actorAddress, partialTick);
        if (swingProgress > 0.0f)
        {
            // The native pose uses an approximately -80 degree X-axis swing.
            // Add only the delta so the game's existing easing and other axes remain intact.
            constexpr float nativeSwingAngleDegrees = -80.0f;
            const float swingFactor = std::sin(std::sqrt(swingProgress) * 3.14159265358979323846f);
            const float deltaRadians = deg2rad(
                g_swingAngleDegrees.load(std::memory_order_relaxed) - nativeSwingAngleDegrees) * swingFactor;
            g_matrixRotate(reinterpret_cast<void*>(matrixScope.matrix), deltaRadians, 1.0f, 0.0f, 0.0f);
        }
    }

    return original(renderer, actor, renderState, hand, partialTick);
}

bool installItemInHandDrawHook()
{
    if (g_itemInHandDrawTarget)
        return true;

    const uintptr_t drawAddress = mem::resolve(off::fn::ItemInHandDraw);
    const uintptr_t pushAddress = mem::resolve(off::fn::RenderMatrixStackPush);
    const uintptr_t translateAddress = mem::resolve(off::fn::RenderMatrixTranslate);
    const uintptr_t rotateAddress = mem::resolve(off::fn::RenderMatrixRotate);
    if (!drawAddress || !pushAddress || !translateAddress || !rotateAddress ||
        !mem::isReadable(drawAddress, 16) || !mem::isReadable(pushAddress, 16) ||
        !mem::isReadable(translateAddress, 16) || !mem::isReadable(rotateAddress, 16))
    {
        MC_LOG_ERROR("[Animations] Item render transform functions are unavailable");
        return false;
    }

    g_matrixStackPush = reinterpret_cast<MatrixStackPushFn>(pushAddress);
    g_matrixTranslate = reinterpret_cast<MatrixTranslateFn>(translateAddress);
    g_matrixRotate = reinterpret_cast<MatrixRotateFn>(rotateAddress);
    void* target = reinterpret_cast<void*>(drawAddress);
    if (!hook::create(target, reinterpret_cast<void*>(&itemInHandDrawDetour),
            reinterpret_cast<void**>(&g_originalItemInHandDraw)))
    {
        g_matrixStackPush = nullptr;
        g_matrixTranslate = nullptr;
        g_matrixRotate = nullptr;
        MC_LOG_ERROR("[Animations] Failed to install item render transform hook");
        return false;
    }

    g_itemInHandDrawTarget = target;
    MC_LOG("[Animations] first-person item transform hook installed @ %p", target);
    return true;
}

void publishRenderSettings(bool moduleEnabled, bool smallItems, bool customAngle,
    bool onlyOnBlock, float angle)
{
    g_smallItemsActive.store(moduleEnabled && smallItems, std::memory_order_relaxed);
    g_customSwingAngleActive.store(moduleEnabled && customAngle, std::memory_order_relaxed);
    g_onlyOnBlockSwingAngle.store(onlyOnBlock, std::memory_order_relaxed);
    g_swingAngleDegrees.store(angle, std::memory_order_relaxed);
}

__int64 __fastcall humanoidModelAnimationSetupDetour(void* model, void* actor)
{
    const auto original = g_originalModelAnimationSetup;
    if (!original)
        return 0;

    const __int64 result = original(model, actor);
    const uintptr_t actorAddress = reinterpret_cast<uintptr_t>(actor);
    if (!model || !actor || actorAddress != Game::get().localAddr())
        return result;
    if (!g_blockPoseActive.load(std::memory_order_relaxed))
    {
        g_blendInitialized = false;
        return result;
    }

    const uintptr_t modelAddress = reinterpret_cast<uintptr_t>(model);
    const uintptr_t rotation = modelAddress + off::model::HumanoidRightArm +
        off::model::ModelPartRotation;
    float baseRotation[3]{};
    if (!mem::isReadable(rotation, sizeof(baseRotation)) ||
        !mem::rawRead(baseRotation, rotation, sizeof(baseRotation)) ||
        !std::isfinite(baseRotation[0]) || !std::isfinite(baseRotation[1]) ||
        !std::isfinite(baseRotation[2]))
    {
        g_blendInitialized = false;
        return result;
    }

    const bool blocking = blockPoseRequested(actorAddress);
    const double now = smooth::detail::nowMs();
    const bool sameModel = g_blendInitialized && g_blendActor == actorAddress &&
        g_blendModel == modelAddress;
    const float dt = sameModel
        ? static_cast<float>((now - g_lastBlendTimeMs) / 1000.0)
        : 0.0f;
    const float alpha = sameModel
        ? 1.0f - std::exp(-18.0f * std::clamp(dt, 0.0f, 0.1f))
        : 0.0f;
    const float target[3] = {
        blocking ? deg2rad(g_blockRotations[0].load(std::memory_order_relaxed)) : baseRotation[0],
        blocking ? deg2rad(g_blockRotations[1].load(std::memory_order_relaxed)) : baseRotation[1],
        blocking ? deg2rad(g_blockRotations[2].load(std::memory_order_relaxed)) : baseRotation[2],
    };

    if (!sameModel)
    {
        std::copy(std::begin(baseRotation), std::end(baseRotation), g_blendedArmRotation);
        g_blendActor = actorAddress;
        g_blendModel = modelAddress;
        g_blendInitialized = true;
    }
    for (int i = 0; i < 3; ++i)
        g_blendedArmRotation[i] += (target[i] - g_blendedArmRotation[i]) * alpha;
    g_lastBlendTimeMs = now;

    if (!mem::rawWrite(rotation, g_blendedArmRotation, sizeof(g_blendedArmRotation)))
        g_blendInitialized = false;
    return result;
}

bool installHumanoidModelAnimationSetupHook()
{
    if (g_modelAnimationSetupTarget)
        return true;

    const uintptr_t address = mem::resolve(off::fn::HumanoidModelAnimationSetup);
    if (!address || !mem::isReadable(address, 16))
    {
        MC_LOG_ERROR("[Animations] Humanoid model animation function is not readable");
        return false;
    }

    void* target = reinterpret_cast<void*>(address);
    if (!hook::create(target,
            reinterpret_cast<void*>(&humanoidModelAnimationSetupDetour),
            reinterpret_cast<void**>(&g_originalModelAnimationSetup)))
    {
        MC_LOG_ERROR("[Animations] Failed to install humanoid model animation hook");
        return false;
    }

    g_modelAnimationSetupTarget = target;
    g_modelSetupHookAvailable.store(true, std::memory_order_relaxed);
    MC_LOG("[Animations] humanoid model animation hook installed @ %p", target);
    return true;
}

void publishBlockSettings(bool active, bool onlyOnBlock, float x, float y, float z)
{
    g_blockPoseActive.store(active && g_modelSetupHookAvailable.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    g_onlyOnBlock.store(onlyOnBlock, std::memory_order_relaxed);
    g_blockRotations[0].store(x, std::memory_order_relaxed);
    g_blockRotations[1].store(y, std::memory_order_relaxed);
    g_blockRotations[2].store(z, std::memory_order_relaxed);
}

}

Animations::Animations()
    : Module("Animations", "Customizes swing timing, first-person item transforms and third-person block pose",
             Category::Visuals, 0)
{
    markHasSettings();
}

void Animations::onEnable()
{
    g_swingTime.store(std::clamp(swingTime_, 0, 20), std::memory_order_relaxed);
    g_active.store(installSwingDurationHook(), std::memory_order_relaxed);
    const bool renderHookAvailable = installItemInHandDrawHook();
    publishRenderSettings(renderHookAvailable, smallItems_, customSwingAngle_,
        onlyOnBlockSwingAngle_, swingAngle_);
    publishBlockSettings(installHumanoidModelAnimationSetupHook() && thirdPersonBlock_,
        onlyOnBlock_, blockXRotation_, blockYRotation_, blockZRotation_);
}

void Animations::onDisable()
{
    g_active.store(false, std::memory_order_relaxed);
    publishRenderSettings(false, smallItems_, customSwingAngle_,
        onlyOnBlockSwingAngle_, swingAngle_);
    g_blockPoseActive.store(false, std::memory_order_relaxed);
}

void Animations::onShutdown()
{
    g_active.store(false, std::memory_order_relaxed);
    publishRenderSettings(false, smallItems_, customSwingAngle_,
        onlyOnBlockSwingAngle_, swingAngle_);
    g_blockPoseActive.store(false, std::memory_order_relaxed);
    if (g_itemInHandDrawTarget)
    {
        hook::remove(g_itemInHandDrawTarget);
        g_itemInHandDrawTarget = nullptr;
        g_originalItemInHandDraw = nullptr;
        g_matrixStackPush = nullptr;
        g_matrixTranslate = nullptr;
        g_matrixRotate = nullptr;
    }
    if (g_modelAnimationSetupTarget)
    {
        hook::remove(g_modelAnimationSetupTarget);
        g_modelAnimationSetupTarget = nullptr;
        g_originalModelAnimationSetup = nullptr;
        g_modelSetupHookAvailable.store(false, std::memory_order_relaxed);
    }
    if (g_swingDurationTarget)
    {
        hook::remove(g_swingDurationTarget);
        g_swingDurationTarget = nullptr;
        g_originalGetSwingDuration = nullptr;
    }
}

void Animations::drawSettings()
{
    if (widgets::CSlider("Swing Time##anim", &swingTime_, 0, 20, "%d ticks"))
        g_swingTime.store(swingTime_, std::memory_order_relaxed);
    if (widgets::Checkbox("Custom Swing Angle##anim", &customSwingAngle_))
        publishRenderSettings(enabled(), smallItems_, customSwingAngle_,
            onlyOnBlockSwingAngle_, swingAngle_);
    if (customSwingAngle_)
    {
        if (widgets::Checkbox("Only on Block##swing-angle", &onlyOnBlockSwingAngle_))
            publishRenderSettings(enabled(), smallItems_, customSwingAngle_,
                onlyOnBlockSwingAngle_, swingAngle_);
        if (widgets::CSlider("Swing Angle##anim", &swingAngle_, -180.0f, 180.0f, "%.2f deg"))
            publishRenderSettings(enabled(), smallItems_, customSwingAngle_,
                onlyOnBlockSwingAngle_, swingAngle_);
    }
    if (widgets::Checkbox("Small Items##anim", &smallItems_))
        publishRenderSettings(enabled(), smallItems_, customSwingAngle_,
            onlyOnBlockSwingAngle_, swingAngle_);
    if (widgets::Checkbox("Third Person Block##anim", &thirdPersonBlock_))
        publishBlockSettings(thirdPersonBlock_, onlyOnBlock_, blockXRotation_,
            blockYRotation_, blockZRotation_);
    if (thirdPersonBlock_)
    {
        if (widgets::Checkbox("Only on Block##anim", &onlyOnBlock_))
            publishBlockSettings(thirdPersonBlock_, onlyOnBlock_, blockXRotation_,
                blockYRotation_, blockZRotation_);
        const bool rotationChanged =
            widgets::CSlider("X Rotation##anim", &blockXRotation_, -180.0f, 180.0f, "%.2f deg") |
            widgets::CSlider("Y Rotation##anim", &blockYRotation_, -180.0f, 180.0f, "%.2f deg") |
            widgets::CSlider("Z Rotation##anim", &blockZRotation_, -180.0f, 180.0f, "%.2f deg");
        if (rotationChanged)
            publishBlockSettings(thirdPersonBlock_, onlyOnBlock_, blockXRotation_,
                blockYRotation_, blockZRotation_);
    }
}

std::string Animations::suffix() const
{
    return std::to_string(swingTime_) + "t";
}

nlohmann::json Animations::saveSettings() const
{
    return nlohmann::json{
        {"swingTime", swingTime_},
        {"customSwingAngle", customSwingAngle_},
        {"onlyOnBlockSwingAngle", onlyOnBlockSwingAngle_},
        {"swingAngle", swingAngle_},
        {"smallItems", smallItems_},
        {"thirdPersonBlock", thirdPersonBlock_},
        {"onlyOnBlock", onlyOnBlock_},
        {"blockXRotation", blockXRotation_},
        {"blockYRotation", blockYRotation_},
        {"blockZRotation", blockZRotation_},
    };
}

void Animations::loadSettings(const nlohmann::json& j)
{
    swingTime_ = std::clamp(j.value("swingTime", swingTime_), 0, 20);
    customSwingAngle_ = j.value("customSwingAngle", customSwingAngle_);
    onlyOnBlockSwingAngle_ = j.value("onlyOnBlockSwingAngle", onlyOnBlockSwingAngle_);
    swingAngle_ = std::clamp(j.value("swingAngle", swingAngle_), -180.0f, 180.0f);
    smallItems_ = j.value("smallItems", smallItems_);
    thirdPersonBlock_ = j.value("thirdPersonBlock", thirdPersonBlock_);
    onlyOnBlock_ = j.value("onlyOnBlock", onlyOnBlock_);
    blockXRotation_ = std::clamp(j.value("blockXRotation", blockXRotation_), -180.0f, 180.0f);
    blockYRotation_ = std::clamp(j.value("blockYRotation", blockYRotation_), -180.0f, 180.0f);
    blockZRotation_ = std::clamp(j.value("blockZRotation", blockZRotation_), -180.0f, 180.0f);
    if (enabled())
    {
        g_swingTime.store(swingTime_, std::memory_order_relaxed);
        publishRenderSettings(enabled() && g_itemInHandDrawTarget, smallItems_,
            customSwingAngle_, onlyOnBlockSwingAngle_, swingAngle_);
        publishBlockSettings(thirdPersonBlock_, onlyOnBlock_, blockXRotation_,
            blockYRotation_, blockZRotation_);
    }
}

MC_REGISTER_MODULE(Animations);

}
