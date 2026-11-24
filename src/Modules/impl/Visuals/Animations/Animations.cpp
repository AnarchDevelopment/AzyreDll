#include "Animations.hpp"

#include "Framework/Hook.hpp"
#include "Framework/Log.hpp"
#include "Framework/Math.hpp"
#include "Framework/Memory.hpp"
#include "GUI/Widgets.hpp"
#include "SDK/Core/Game.hpp"
#include "SDK/Entity/LocalPlayer.hpp"
#include "SDK/Engine/Memory/Offsets.hpp"
#include "SDK/Engine/VTable/VTables.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <vector>
#include <windows.h>
#include <intrin.h>

namespace mc {
namespace {

// The native first person swing feeds three rotations of the Java 1.8 item
// transform:
//   Y <- sin(p*p*pi)   * -20   (call site 0x2FBBD2)
//   Z <- sin(sqrt(p)*pi) * -20  (call site 0x2FBBF9)
//   X <- sin(sqrt(p)*pi) * -80  (call site 0x2FBC21)
// so angle / -80 on the X call site recovers the envelope f2 and angle / -20 on
// the Y call site recovers the square term sin(p*p*pi) without rebuilding it.
constexpr float kNativeSwingAngle = -80.0f;
constexpr float kNativeSwingLiftAngle = -20.0f;

// Rise 6.9.5 "Block Animation" presets (module/impl/render/Animations.java).
//
// Rise handles the first person item by *replacing* the vanilla chain: every
// preset in Rise starts with itemrenderer.transformFirstPersonItem(equip, swing),
// then scale(d1), then its own transforms and ends with
// itemrenderer.doBlockTransformations(). This build already applies that base
// itself (translate(0.56, -0.52, -0.72) + rotate(45, Y), see the RVAs in
// FunctionRvas.hpp) and reaches the end of the chain through the scale call at
// 0x1402FBC3B, so:
//
//  * The presets are appended there, which is exactly Rise's position: after the
//    base transform, before the guard. transformFirstPersonItem and scale(d1)
//    are therefore NOT part of the tables (d1 is the user scale, 1 by default).
//  * The swing trio that the base carries is silenced for every preset whose
//    Java body passes swing = 0 (or 1, where both curves are zero), so the pose
//    replaces the swing arc like Java 1.7/1.8 does. Presets that pass f1 keep it.
//  * The pending native scale (0.4) shrinks the item and every translation
//    together, so the offsets stay in the same ratio to the item as in Rise: no
//    gain is applied to translations (compensating it made the poses fly out of
//    the view, which was the bug).
//  * GlStateManager.rotate normalizes its axis and sub_140088410 does not, so
//    the axis of every rotation is normalized here (Rise hands over axes such as
//    (0, 15, 200) or (-8, 0, 9)).
//
// doBlockTransformations() is reproduced as the Block op at the exact position
// Rise invokes it; Bedrock 0.15 has no sword-block pose of its own.
//
// Dynamic values keep their curves: f2 = sin(sqrt(p)*pi) (the envelope above),
// f4/f5/f7/f8 = sin(p*p*pi) (taken live from the native Y rotation), f3 = 0.8p -
// 0.8p*p and f12 = sin(p*p*pi - 3) with the progress p rebuilt from f2 as
// (asin(f2)/pi)^2.
enum PresetCurve : unsigned char { CurveStatic, CurveF2, CurveF3, CurveF4, CurveF12, CurveTime };

struct PresetOp
{
    enum Kind : unsigned char { Translate, Rotate, Scale, Block };

    Kind kind;
    PresetCurve curve;
    // Bits 0/1/2 mark axis components that ride the op curve, like Rise's
    // (-f2, 0, 9) axis.
    unsigned char dynamicAxis;
    // Translate: xyz. Rotate: degrees + axis. Scale: uniform.
    float a;
    float b;
    float c;
    float d;
};

constexpr PresetOp opT(float x, float y, float z)
{
    return {PresetOp::Translate, CurveStatic, 0, x, y, z, 0.0f};
}

constexpr PresetOp opTC(PresetCurve curve, float x, float y, float z)
{
    return {PresetOp::Translate, curve, 0, x, y, z, 0.0f};
}

constexpr PresetOp opR(float degrees, float x, float y, float z)
{
    return {PresetOp::Rotate, CurveStatic, 0, degrees, x, y, z};
}

constexpr PresetOp opRC(PresetCurve curve, float degrees, float x, float y, float z,
    unsigned char dynamicAxis = 0)
{
    return {PresetOp::Rotate, curve, dynamicAxis, degrees, x, y, z};
}

constexpr PresetOp opS(float uniform)
{
    return {PresetOp::Scale, CurveStatic, 0, uniform, uniform, uniform, 0.0f};
}

// ItemRenderer.doBlockTransformations() from the Java 1.7/1.8 renderer. The z
// offset is 0.02, not 0: that is what pushes the guard away from the hand.
constexpr PresetOp opB()
{
    return {PresetOp::Block, CurveStatic, 0, 0.0f, 0.0f, 0.0f, 0.0f};
}

// "Stab" spins on the wall clock in Rise: rotate(-(currentTimeMillis / 2 % 360), X).
constexpr PresetOp opRT()
{
    return {PresetOp::Rotate, CurveTime, 0, 0.0f, 1.0f, 0.0f, 0.0f};
}

// "None", "Sunny", "1.7" and the default all reduce to the guard itself. "1.7"
// additionally calls a private ItemRenderer helper (JX) that has no equivalent
// here, so it is approximated by the guard.
constexpr PresetOp kOpsBlockOnly[] = {opB()};

constexpr PresetOp kOpsAccurate[] = {
    opT(0.05f, -0.05f, -0.12f), opS(0.99f), opB(), opT(-0.5f, 0.2f, 0.0f)};

constexpr PresetOp kOpsLucid[] = {
    opT(0.0f, 0.03f, -0.05f), opRC(CurveF2, 15.0f, -1.0f, 0.0f, 9.0f, 0x1),
    opRC(CurveF2, 40.0f, 1.0f, -0.5f, 0.0f, 0x2), opB()};

// Astro builds its own anchor in Rise (translate(0.56, -0.52, -0.72) + rotate(45,
// Y)) which is exactly the base this build already applied, so those two steps
// are dropped and only the offset that comes before them survives.
constexpr PresetOp kOpsAstro[] = {
    opT(0.0f, 0.3f, 0.0f), opRC(CurveF3, -90.0f, 0.0f, 1.0f, 0.0f), opS(0.37f), opB()};

// The guard comes first here: Rise rotates the guard itself after it is applied.
constexpr PresetOp kOpsTap[] = {
    opB(), opRC(CurveF4, -20.0f, 0.0f, 1.0f, 0.0f),
    opRC(CurveF2, -20.0f, 0.0f, 0.0f, 1.0f), opRC(CurveF2, -80.0f, 1.0f, 0.0f, 0.0f)};

// The last translation depends on sneaking in Rise (-0.1 vs -0.2); the crouch
// value is kept so the pose never changes under the player's feet.
constexpr PresetOp kOpsDortware[] = {
    opT(0.0f, 0.3f, 0.0f), opRC(CurveF4, -27.5f, -8.0f, 0.0f, 9.0f),
    opRC(CurveF4, -45.0f, 1.0f, 0.5f, 0.0f, 0x2), opB(), opT(1.2f, 0.3f, 0.5f),
    opT(-1.0f, -0.2f, 0.2f)};

// XIV also re-anchors the item by hand (translate + rotate(45, Y) + its own trio
// + scale(0.4)); the anchor is the base again, so it starts at its own trio.
constexpr PresetOp kOpsXIV[] = {
    opRC(CurveF4, -20.0f, 0.0f, 1.0f, 0.0f), opRC(CurveF2, -20.0f, 0.0f, 0.0f, 1.0f),
    opRC(CurveF2, -40.0f, 1.0f, 0.0f, 0.0f), opS(0.4f), opB()};

// f9 = -2 * f2 in Rise, folded into the constants: f9 / 10 + 0.1 becomes a
// static 0.1 plus -0.2 * f2, f9 * 10 becomes -20 * f2 and -f9 * 20 becomes 40 * f2.
constexpr PresetOp kOpsAvatar[] = {
    opT(0.0f, 0.1f, 0.0f), opTC(CurveF2, 0.0f, -0.2f, 0.0f),
    opRC(CurveF2, -20.0f, 0.0f, 1.0f, 0.0f), opR(250.0f, 0.2f, 1.0f, -0.6f),
    opR(-10.0f, 1.0f, 0.5f, 1.0f), opRC(CurveF2, 40.0f, 1.0f, 0.5f, 1.0f)};

// "Smooth" is the wide spinning pose in Rise (6090/6085/6110 degrees).
constexpr PresetOp kOpsSmooth[] = {
    opT(0.6f, 0.3f, -0.6f), opTC(CurveF2, 0.0f, 0.0f, -0.7f), opR(6090.0f, 0.0f, 0.0f, 0.1f),
    opR(6085.0f, 0.0f, 0.1f, 0.0f), opR(6110.0f, 0.1f, 0.0f, 0.0f), opB()};

// "Stab" never calls the guard in Rise and keeps the wall-clock spin on X.
constexpr PresetOp kOpsStab[] = {
    opT(0.0f, 0.2f, -1.0f), opR(-59.0f, -1.0f, 0.0f, 3.0f), opRT(),
    opR(60.0f, 0.0f, 1.0f, 0.0f)};

constexpr PresetOp kOpsSpin[] = {
    opT(0.0f, -0.03f, -0.13f), opT(0.0f, 0.1f, 0.0f), opB(),
    opRC(CurveF2, 10.0f, 0.0f, 1.0f, 1.5f), opRC(CurveF2, -50.0f, 1.0f, 0.9f, 0.0f)};

constexpr PresetOp kOpsLeaked[] = {opT(0.0f, 0.1f, 0.0f), opB()};

constexpr PresetOp kOpsOld[] = {
    opT(0.0f, -0.05f, 0.0f), opT(0.0f, 0.3f, 0.0f),
    opRC(CurveF2, -31.0f, 1.0f, 0.0f, 2.0f),
    opRC(CurveF2, -33.0f, 1.5f, 0.909f, 0.0f, 0x2), opB()};

constexpr PresetOp kOpsExhibition[] = {
    opT(0.0f, -0.05f, 0.0f), opT(-0.04f, 0.13f, 0.0f),
    opRC(CurveF2, -20.0f, 0.5f, 1.0f, 4.0f, 0x1),
    opRC(CurveF2, -30.0f, 1.0f, 0.333f, 0.0f, 0x2), opB()};

constexpr PresetOp kOpsExhibitionOld[] = {
    opT(0.0f, -0.04f, -0.01f), opT(0.0f, 0.3f, 0.0f),
    opRC(CurveF2, -30.0f, 1.0f, 0.0f, 2.0f),
    opRC(CurveF2, -44.0f, 1.5f, 0.833f, 0.0f, 0x2), opB()};

constexpr PresetOp kOpsExhibitionNew[] = {
    opT(0.0f, 0.1f, -0.05f), opRC(CurveF2, 30.0f, -1.0f, 0.0f, 9.0f, 0x1),
    opRC(CurveF2, 40.0f, 1.0f, -1.0f, 0.0f, 0x2), opB()};

constexpr PresetOp kOpsSwong[] = {
    opT(-0.5f, 0.4f, -0.2f), opR(30.0f, 0.0f, 1.0f, 0.0f), opR(-70.0f, 1.0f, 0.0f, 0.0f),
    opR(40.0f, 0.0f, 1.0f, 0.0f)};

constexpr PresetOp kOpsStella[] = {
    opT(0.0f, 0.1f, -0.05f), opB(), opT(-0.05f, 0.2f, 0.0f),
    opRC(CurveF2, -35.0f, -8.0f, 0.0f, 9.0f), opRC(CurveF2, -70.0f, 1.0f, -0.4f, 0.0f)};

// The second rotation uses ConstantManager.J, which is not recoverable from the
// decompiled Java, so the 70 degree default is kept.
constexpr PresetOp kOpsFlup[] = {
    opB(), opT(-0.05f, 0.3f, 0.3f), opRC(CurveF2, -140.0f, 8.0f, 0.0f, 8.0f),
    opRC(CurveF2, 70.0f, 8.0f, 0.0f, 8.0f)};

// "Noov" calls transformFirstPersonItem(-0.25F, 1 + f2/10) in Rise: the equip
// argument is a constant -0.25, so the anchor sits +0.75 above the native one
// (handled by BlockPreset::anchorY) and only its own X rotation is left.
constexpr PresetOp kOpsNoov[] = {opRC(CurveF2, -25.0f, 1.0f, 0.0f, 0.0f), opB()};

constexpr PresetOp kOpsKomorebi[] = {
    opT(0.41f, -0.25f, -0.5556f), opR(35.0f, 0.0f, 1.5f, 0.0f),
    opRC(CurveF2, -12.0f, 0.0f, 0.0f, 1.0f), opRC(CurveF2, -65.0f, 1.0f, 0.0f, 0.0f), opB()};

// "Rhys": transformFirstPersonItem(f, f1), scale, two translates, guard.
constexpr PresetOp kOpsRhys[] = {
    opT(-0.35f, 0.1f, 0.0f), opT(-0.05f, -0.1f, 0.1f), opB()};

// "Swing": the guard first, then the item is pulled back by (-0.3, -0.1, 0).
constexpr PresetOp kOpsSwing[] = {opB(), opT(-0.3f, -0.1f, 0.0f)};

// "?" closes on sin(f1*f1*pi - 3), which is the F12 curve reconstructed from
// the recovered progress.
constexpr PresetOp kOpsQuestion[] = {
    opRC(CurveF2, -10.0f, 0.0f, 15.0f, 200.0f),
    opRC(CurveF2, -10.0f, 300.0f, 0.5f, 1.0f, 0x2), opB(), opT(2.4f, 0.3f, 0.5f),
    opT(-2.1f, -0.2f, 0.1f), opRC(CurveF12, 13.0f, -10.0f, -1.4f, -10.0f)};

// "Beta" and "Dortware" share the same body in Rise.
constexpr PresetOp kOpsBeta[] = {
    opT(0.0f, 0.3f, 0.0f), opRC(CurveF4, -27.5f, -8.0f, 0.0f, 9.0f),
    opRC(CurveF4, -45.0f, 1.0f, 0.5f, 0.0f, 0x2), opB(), opT(1.2f, 0.3f, 0.5f),
    opT(-1.0f, -0.2f, 0.2f)};

struct BlockPreset
{
    const char* name;
    const PresetOp* ops;
    unsigned count;
    // true when the Java body calls transformFirstPersonItem(equip, f1), i.e.
    // the preset keeps the swing arc (rebuilt as the Java 1.8 one because the
    // native chain swaps its first two curves); false when it passes 0 (or 1,
    // where both curves are zero) and the pose therefore replaces the arc.
    bool keepSwing;
    // Rise sometimes hands a modified equippedProgress to
    // transformFirstPersonItem. That progress is the -0.6 term of the anchor
    // translate, and the native chain already applied it untouched, so this is
    // the residual offset ((equip - 1) * -0.6) the pose has to restore.
    float anchorY;
};

constexpr BlockPreset kBlockPresets[] = {
    // "Custom" is the module's own triple of sliders, not a Rise mode.
    {"Custom", nullptr, 0, false, 0.0f},
    // transformFirstPersonItem(f, 0) + guard: the item just sits in the guard.
    {"None", kOpsBlockOnly, 1, false, 0.0f},
    {"1.7", kOpsBlockOnly, 1, true, 0.0f},
    {"1.7 Accurate", kOpsAccurate, 4, true, -0.09f},
    {"Sunny", kOpsBlockOnly, 1, true, 0.06f},
    {"Lucid", kOpsLucid, 4, true, 0.30f},
    {"Astro", kOpsAstro, 4, false, 0.60f},
    {"Tap", kOpsTap, 4, false, 0.0f},
    {"Beta", kOpsBeta, 6, false, 0.30f},
    {"XIV", kOpsXIV, 5, false, 0.66f},
    {"Avatar", kOpsAvatar, 6, false, 0.0f},
    {"Smooth", kOpsSmooth, 6, false, 0.60f},
    {"Stab", kOpsStab, 4, false, 0.0f},
    {"Spin", kOpsSpin, 5, false, 0.40f},
    {"Leaked", kOpsLeaked, 2, true, 0.42f},
    {"Old", kOpsOld, 5, false, 0.30f},
    {"Exhibition", kOpsExhibition, 5, false, 0.36f},
    {"Exhibition Old", kOpsExhibitionOld, 5, false, 0.30f},
    {"Exhibition New", kOpsExhibitionNew, 4, true, 0.30f},
    {"Swong", kOpsSwong, 4, true, 0.0f},
    {"Stella", kOpsStella, 5, false, 0.0f},
    {"Flup", kOpsFlup, 4, false, 0.20f},
    {"Noov", kOpsNoov, 2, false, 0.75f},
    {"Komorebi", kOpsKomorebi, 5, false, 0.0f},
    {"Rhys", kOpsRhys, 3, true, 0.0f},
    {"Swing", kOpsSwing, 2, true, 0.0f},
    {"?", kOpsQuestion, 6, false, 0.0f},
    {"Dortware", kOpsDortware, 6, false, 0.30f},
};

constexpr int kBlockPresetCount =
    static_cast<int>(sizeof(kBlockPresets) / sizeof(kBlockPresets[0]));


using SwingDurationFn = int(__fastcall*)(void* actor);
using MatrixTranslateFn = void(__fastcall*)(void* matrix, float x, float y, float z);
using MatrixRotateFn = void(__fastcall*)(void* matrix, float angleDegrees,
    float axisX, float axisY, float axisZ);
using MatrixScaleFn = void(__fastcall*)(void* matrix, float x, float y, float z);
using AnimationSetupFn = __int64(__fastcall*)(void* renderer, void* actor,
    float partialTick);
using ModelPartRenderFn = void(__fastcall*)(void* part, void* model, float scale);

SwingDurationFn g_originalSwingDuration = nullptr;
// The item transform table needs the translate helper as well. It is not
// hooked (nothing has to be overridden there): the resolved original is enough
// and it is called directly while composing a preset.
MatrixTranslateFn g_matrixTranslate = nullptr;
MatrixRotateFn g_originalMatrixRotate = nullptr;
MatrixScaleFn g_originalMatrixScale = nullptr;
AnimationSetupFn g_originalAnimationSetup = nullptr;
ModelPartRenderFn g_originalModelPartRender = nullptr;

void* g_swingDurationTarget = nullptr;
void* g_swingRotateTarget = nullptr;
void* g_swingScaleTarget = nullptr;
void* g_animationSetupTarget = nullptr;
void* g_modelPartTarget = nullptr;

// The matrix helpers are shared by the item renderer and the model part
// renderer, so every override is scoped to the verified first person swing
// sequence by return address: the three swing rotations of the item chain and
// the 0.4 scale that closes it.
uintptr_t g_swingLiftReturn = 0;
uintptr_t g_swingTwistReturn = 0;
uintptr_t g_swingRotateReturn = 0;
uintptr_t g_swingScaleReturn = 0;

std::atomic<bool> g_swingTimeActive{false};
std::atomic<int> g_swingTime{6};
std::atomic<bool> g_customSwingAngle{false};
std::atomic<bool> g_fluxSwing{false};
std::atomic<float> g_swingAngleDegrees{-31.0f};
std::atomic<bool> g_onlyOnBlockSwingAngle{false};
std::atomic<bool> g_smallItemsActive{false};
std::atomic<float> g_smallItemsScale{0.75f};
std::atomic<bool> g_blockPoseActive{false};
std::atomic<bool> g_onlyOnBlock{true};
std::atomic<float> g_blockRotations[3]{-57.0f, -45.16f, 13.14f};
std::atomic<int> g_blockPreset{0};
// Live factors of the native swing, read from the three rotate call sites:
//   g_swingEnvelope     = sin(sqrt(p)*pi)  (f2, from the -80 X rotation)
//   g_swingSquareFactor = sin(p*p*pi)      (f4/f7, from the -20 Y rotation)
// The stamp tells the item scale (which runs later in the same frame) whether a
// swing is being rendered at all.
std::atomic<float> g_swingEnvelope{0.0f};
std::atomic<float> g_swingSquareFactor{0.0f};
std::atomic<uint64_t> g_swingEnvelopeStamp{0};

// The local player can be rendered through more than one model instance per
// frame (skin layer, held item layer, ...). Every HumanoidModel seen while the
// local actor is being set up is remembered so the pose never depends on
// catching the same instance the parts are drawn with.
constexpr unsigned kMaxLocalModels = 4;
std::atomic<uintptr_t> g_localModels[kMaxLocalModels]{};
std::atomic<unsigned> g_localModelCount{0};
std::atomic<unsigned> g_localModelCursor{0};

bool isLocalModel(uintptr_t model)
{
    const unsigned count = g_localModelCount.load(std::memory_order_relaxed);
    for (unsigned i = 0; i < count && i < kMaxLocalModels; ++i)
    {
        if (g_localModels[i].load(std::memory_order_relaxed) == model)
            return true;
    }
    return false;
}

void rememberLocalModel(uintptr_t model)
{
    if (!model || isLocalModel(model))
        return;
    const unsigned count = g_localModelCount.load(std::memory_order_relaxed);
    if (count < kMaxLocalModels)
    {
        g_localModels[count].store(model, std::memory_order_relaxed);
        g_localModelCount.store(count + 1, std::memory_order_relaxed);
        return;
    }
    // Full: recycle the oldest slot so a respawn or a dimension change can
    // never starve the set after four different model instances.
    const unsigned slot =
        g_localModelCursor.fetch_add(1, std::memory_order_relaxed) % kMaxLocalModels;
    g_localModels[slot].store(model, std::memory_order_relaxed);
}

void forgetLocalModels()
{
    g_localModelCount.store(0, std::memory_order_relaxed);
    g_localModelCursor.store(0, std::memory_order_relaxed);
    for (unsigned i = 0; i < kMaxLocalModels; ++i)
        g_localModels[i].store(0, std::memory_order_relaxed);
}

// One-shot diagnostics so the pose and the flux curve can be confirmed in the
// log without spamming the render thread.
std::atomic<bool> g_blockPoseLogged{false};
std::atomic<bool> g_blockFirstPersonLogged{false};
std::atomic<bool> g_blockPresetLogged{false};
std::atomic<bool> g_modelCapturedLogged{false};
std::atomic<unsigned> g_fluxLogCount{0};

bool isHoldingItem(bool requireRightClick)
{
    const uintptr_t actor = Game::get().localAddr();
    if (!actor)
        return false;
    if (requireRightClick && (GetAsyncKeyState(VK_RBUTTON) & 0x8000) == 0)
        return false;

    const ItemInstance held = LocalPlayer(actor).heldItem();
    const uintptr_t item = held.valid() ? held.item() : 0;
    return item && mem::isReadable(item, sizeof(uintptr_t));
}

// Swing duration getter (sub_1404DA4A0). sub_1404DA590 divides the swing tick
// counter by this value to build the progress the model consumes, and
// ActorSwing uses half of it as the restart threshold, so overriding it retimes
// both the animation and the client swing cadence.
int __fastcall swingDurationDetour(void* actor)
{
    const auto original = g_originalSwingDuration;
    if (!original)
        return 6;

    const int native = original(actor);
    if (!g_swingTimeActive.load(std::memory_order_relaxed) || !actor)
        return native;
    if (reinterpret_cast<uintptr_t>(actor) != Game::get().localAddr())
        return native;
    return std::clamp(g_swingTime.load(std::memory_order_relaxed), 1, 20);
}

// How the swing arc has to behave while a block preset is rendered. Rise never
// keeps the vanilla chain: every preset body calls transformFirstPersonItem
// itself, so the arc it shows is the Java 1.8 one (square curve on Z, envelope
// on Y and X) or no arc at all. The native Bedrock chain swaps the first two
// curves, so a preset that keeps the swing has to go through the rebuild to
// look like Rise.
enum BlockSwingMode : unsigned char { BlockSwingNative, BlockSwingJava, BlockSwingNone };

BlockSwingMode blockSwingMode(bool& blockingOut)
{
    blockingOut = false;
    if (!g_blockPoseActive.load(std::memory_order_relaxed))
        return BlockSwingNative;

    const int presetIndex = g_blockPreset.load(std::memory_order_relaxed);
    if (presetIndex <= 0 || presetIndex >= kBlockPresetCount)
        return BlockSwingNative;

    blockingOut = g_onlyOnBlock.load(std::memory_order_relaxed)
        ? isHoldingItem(true)
        : isHoldingItem(false);
    if (!blockingOut)
        return BlockSwingNative;

    // transformFirstPersonItem(equip, f1) keeps the arc; (equip, 0) replaces it.
    // Additionally, if the preset keeps the swing arc, check if we need to apply
    // the equip progress residual (-0.6 term) as the anchor Y offset. The native
    // chain already applies it untouched, so the pose has to restore the difference.
    bool keepSwing = kBlockPresets[presetIndex].keepSwing;
    if (keepSwing && presetIndex != 0 && g_matrixTranslate && presetIndex < kBlockPresetCount)
    {
        // Rise sometimes hands a modified equippedProgress to transformFirstPersonItem.
        // That progress is the -0.6 term of the anchor translate, and the native chain
        // already applied it untouched, so this is the residual offset the pose has to
        // restore (still before the pose, and along Y so the 45 degree yaw of the base
        // cannot skew it). We apply it conditionally for non-Custom presets.
    }

    return keepSwing ? BlockSwingJava : BlockSwingNone;
}

void __fastcall matrixRotateDetour(void* matrix, float angleDegrees,
    float axisX, float axisY, float axisZ)
{
    const auto original = g_originalMatrixRotate;
    if (!original)
        return;

    const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const bool liftSite = returnAddress == g_swingLiftReturn;    // Y <- sin(p*p*pi) * -20
    const bool twistSite = returnAddress == g_swingTwistReturn;  // Z <- sin(sqrt(p)*pi) * -20
    const bool swingSite = returnAddress == g_swingRotateReturn; // X <- sin(sqrt(p)*pi) * -80
    if (!liftSite && !twistSite && !swingSite)
    {
        original(matrix, angleDegrees, axisX, axisY, axisZ);
        return;
    }

    // Both live factors are published before any suppression: every preset
    // curve and the flux arc are rebuilt from them, and the suppression below
    // is exactly what removes the rotations that carry them.
    if (liftSite)
        g_swingSquareFactor.store(
            clampf(angleDegrees / kNativeSwingLiftAngle, -1.0f, 1.0f),
            std::memory_order_relaxed);

    const float envelope = clampf(angleDegrees / kNativeSwingAngle, 0.0f, 1.0f);
    if (swingSite)
    {
        g_swingEnvelope.store(envelope, std::memory_order_relaxed);
        g_swingEnvelopeStamp.store(GetTickCount64(), std::memory_order_relaxed);
    }

    bool blocking = false;
    BlockSwingMode mode = blockSwingMode(blocking);
    if (mode == BlockSwingNative && g_fluxSwing.load(std::memory_order_relaxed))
        mode = BlockSwingJava; // flux rebuilds the arc instead of retiming it

    if (mode != BlockSwingNative)
    {
        // None of the three native rotations reach the matrix. The last one (X)
        // is where the arc is rebuilt, in the same order Java 1.8 applies it,
        // so the swing still starts and ends where the native one does.
        if (twistSite || liftSite)
            return;

        if (mode == BlockSwingJava)
        {
            const float square = g_swingSquareFactor.load(std::memory_order_relaxed);
            const bool customAngle = blocking ? false :
                g_customSwingAngle.load(std::memory_order_relaxed) &&
                (!g_onlyOnBlockSwingAngle.load(std::memory_order_relaxed) ||
                    isHoldingItem(true));
            const float peak = customAngle
                ? g_swingAngleDegrees.load(std::memory_order_relaxed)
                : kNativeSwingAngle;

            // Java 1.8 transformFirstPersonItem: the square curve drives Z and
            // the envelope drives Y and X.
            // Normalize the axis like GlStateManager.rotate does: sub_140088410 does
            // not normalize, so we do it here to avoid distorted rotations with
            // off-unit axes such as (0, 15, 200) or (-8, 0, 9).
            float ax = 0.0f, ay = 0.0f, az = 1.0f;
            float length = std::sqrt(ax * ax + ay * ay + az * az);
            if (length > 0.0005f)
            {
                ax /= length; ay /= length; az /= length;
            }
            if (square != 0.0f)
                original(matrix, square * kNativeSwingLiftAngle, ax, ay, az);
            original(matrix, envelope * kNativeSwingLiftAngle, 0.0f, 1.0f, 0.0f);
            original(matrix, envelope * peak, 1.0f, 0.0f, 0.0f);

            const unsigned seen = g_fluxLogCount.fetch_add(1, std::memory_order_relaxed);
            if (seen < 5)
                MC_LOG("[Animations] rebuilt arc: square %.3f envelope %.3f peak %.1f (flux=%d)",
                    square, envelope, peak, g_fluxSwing.load(std::memory_order_relaxed));
        }
        return;
    }

    // Custom swing angle: only the X rotation of the native arc is rescaled.
    const bool customAngle = g_customSwingAngle.load(std::memory_order_relaxed);
    if (!swingSite || !customAngle ||
        (g_onlyOnBlockSwingAngle.load(std::memory_order_relaxed) && !isHoldingItem(true)))
    {
        original(matrix, angleDegrees, axisX, axisY, axisZ);
        return;
    }

    const float peak = g_swingAngleDegrees.load(std::memory_order_relaxed);
    // Normalize the swing X-axis like GlStateManager.rotate normalizes its axis.
    float sx = axisX, sy = axisY, sz = axisZ;
    float slen = std::sqrt(sx * sx + sy * sy + sz * sz);
    if (slen > 0.0005f)
    {
        sx /= slen; sy /= slen; sz /= slen;
    }
    original(matrix, peak * envelope, sx, sy, sz);
}

// Rise drives its dynamic values from the swing progress (f1) through a handful
// of curves. This build exposes two of them live from the native chain
// (f2 = sin(sqrt(f1)*pi) from the X rotation and f4 = sin(f1*f1*pi) from the Y
// rotation); the rest are evaluated on a rebuilt progress
// f1 = (asin(f2)/pi)^2.
float presetCurveValue(PresetCurve curve, float f2)
{
    if (curve == CurveStatic)
        return 1.0f;
    if (curve == CurveF2)
        return f2;
    if (curve == CurveF4)
        // sin(f1*f1*pi) is measured directly on the native Y rotation, so the
        // presets that use it (Tap, Beta, Dortware, XIV) get the exact value
        // even while the native rotation is suppressed.
        return g_swingSquareFactor.load(std::memory_order_relaxed);
    if (curve == CurveTime)
        return 1.0f;

    const float clamped = clampf(f2, 0.0f, 1.0f);
    const float sqrtProgress = std::asin(clamped) / 3.14159265f;
    const float progress = sqrtProgress * sqrtProgress;
    switch (curve)
    {
    case CurveF3:
        return 0.8f * progress - 0.8f * progress * progress;
    case CurveF12:
        return std::sin(progress * progress * 3.14159265f - 3.0f);
    default:
        return 1.0f;
    }
}

// Recover the native progress from both independent swing curves. The X
// envelope alone is ambiguous after p=0.25 because sin(sqrt(p) * pi) starts
// descending; the Y curve disambiguates the two possible phases.
float recoverSwingProgress(float envelope, float squareFactor)
{
    constexpr float kPi = 3.14159265f;
    const float q = std::asin(clampf(envelope, 0.0f, 1.0f)) / kPi;
    const float candidateA = q * q;
    const float otherQ = 1.0f - q;
    const float candidateB = otherQ * otherQ;
    const float observedSquare = clampf(squareFactor, 0.0f, 1.0f);
    const float predictedA = std::sin(candidateA * candidateA * kPi);
    const float predictedB = std::sin(candidateB * candidateB * kPi);
    return std::fabs(predictedA - observedSquare) <=
                   std::fabs(predictedB - observedSquare)
               ? candidateA
               : candidateB;
}

// Rise's doItemUsedTransformations(f1): the small travel of the item that makes
// the swing look like the hand is pushing it instead of pivoting it.
void applyFluxDip(void* matrix, float envelope, float squareFactor)
{
    if (!g_matrixTranslate)
        return;
    const float e = clampf(envelope, 0.0f, 1.0f);
    if (e <= 0.0f)
        return;

    const float progress = recoverSwingProgress(e, squareFactor);
    const float root = std::sqrt(progress); // sqrt(f1)
    const float dipX = -0.4f * e;
    const float dipY = 0.2f * std::sin(2.0f * 3.14159265f * root);
    const float dipZ = -0.2f * std::sin(3.14159265f * progress);
    g_matrixTranslate(matrix, dipX, dipY, dipZ);
}

// Composes one Rise preset onto the item matrix, in the same order the Java
// client applies it. No gain is applied to the translations: the native 0.4
// item scale that follows this call site shrinks the item and its offsets by
// the same factor, which is exactly the ratio Rise has (it draws the item at
// scale 1 with the same offsets). Feeding 1/0.4 was the bug that pushed the
// poses out of the view.
void applyPresetOps(void* matrix, const PresetOp* ops, unsigned count, float envelope)
{
    const auto rotate = g_originalMatrixRotate;
    const auto scale = g_originalMatrixScale;
    if (!rotate || !scale || !ops)
        return;

    for (unsigned i = 0; i < count; ++i)
    {
        const PresetOp& op = ops[i];
        const float k = presetCurveValue(op.curve, envelope);
        switch (op.kind)
        {
        case PresetOp::Translate:
            if (g_matrixTranslate && k != 0.0f &&
                (op.a != 0.0f || op.b != 0.0f || op.c != 0.0f))
            {
                g_matrixTranslate(matrix, op.a * k, op.b * k, op.c * k);
            }
            break;
        case PresetOp::Rotate:
        {
            // "Stab" spins on the wall clock in Rise, not on the swing progress.
            const float degrees = op.curve == CurveTime
                ? -static_cast<float>(GetTickCount64() / 2 % 360)
                : op.a * k;
            float ax = (op.dynamicAxis & 0x1) ? op.b * k : op.b;
            float ay = (op.dynamicAxis & 0x2) ? op.c * k : op.c;
            float az = (op.dynamicAxis & 0x4) ? op.d * k : op.d;
            const float length = std::sqrt(ax * ax + ay * ay + az * az);
            // GlStateManager.rotate normalizes the axis; sub_140088410 feeds it
            // straight into the axis-angle matrix, so an off-unit axis would
            // distort the rotation.
            if (std::fabs(degrees) < 0.0005f || length < 0.0005f)
                break;
            rotate(matrix, degrees, ax / length, ay / length, az / length);
            break;
        }
        case PresetOp::Scale:
            scale(matrix, op.a, op.b, op.c);
            break;
        case PresetOp::Block:
            // doBlockTransformations(): translate(-0.5, 0.2, 0.02), rotate(30,
            // Y), rotate(-80, X), rotate(60, Y).
            if (g_matrixTranslate)
                g_matrixTranslate(matrix, -0.5f, 0.2f, 0.02f);
            rotate(matrix, 30.0f, 0.0f, 1.0f, 0.0f);
            rotate(matrix, -80.0f, 1.0f, 0.0f, 0.0f);
            rotate(matrix, 60.0f, 0.0f, 1.0f, 0.0f);
            break;
        }
    }
}

void __fastcall matrixScaleDetour(void* matrix, float x, float y, float z)
{
    const auto original = g_originalMatrixScale;
    if (!original)
        return;

    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) != g_swingScaleReturn)
    {
        original(matrix, x, y, z);
        return;
    }

    // This is the last transform of the first person item, right before the
    // draw, so it runs on every frame whether the player is swinging or
    // standing still. Composing the guard here makes the pose independent of
    // the animation timeline. sub_140088410 takes DEGREES (the model part path
    // converts its radians with 180/pi before calling it), so the sliders and
    // the preset table are used as-is.
    float factor = 1.0f;
    if (g_smallItemsActive.load(std::memory_order_relaxed))
        factor = g_smallItemsScale.load(std::memory_order_relaxed);

    // The three rotate call sites run every frame the item is drawn, so a stale
    // stamp means no swing is being rendered and every dynamic curve decays to
    // its rest value instead of freezing halfway.
    const uint64_t stamp = g_swingEnvelopeStamp.load(std::memory_order_relaxed);
    const bool fresh = stamp != 0 && GetTickCount64() - stamp <= 100;
    const float envelope = fresh ? g_swingEnvelope.load(std::memory_order_relaxed) : 0.0f;

    const bool blocking = g_blockPoseActive.load(std::memory_order_relaxed) &&
        (g_onlyOnBlock.load(std::memory_order_relaxed) ? isHoldingItem(true)
                                                      : isHoldingItem(false));
    const int presetIndex = g_blockPreset.load(std::memory_order_relaxed);
    const bool hasPreset = presetIndex > 0 && presetIndex < kBlockPresetCount;

    if (blocking && hasPreset)
    {
        const BlockPreset& preset = kBlockPresets[presetIndex];

        // Java passes a modified equippedProgress to transformFirstPersonItem,
        // and that progress is the -0.6 term of the anchor translate. The
        // native chain already applied it with the untouched progress, so the
        // difference is restored here (still before the pose, and along Y so
        // the 45 degree yaw of the base cannot skew it).
        if (g_matrixTranslate && preset.anchorY != 0.0f)
            g_matrixTranslate(matrix, 0.0f, preset.anchorY, 0.0f);

        applyPresetOps(matrix, preset.ops, preset.count, envelope);

        bool expected = false;
        if (g_blockPresetLogged.compare_exchange_strong(expected, true))
            MC_LOG("[Animations] block preset '%s' applied (first person, arc %d)",
                preset.name, preset.keepSwing ? 1 : 0);
    }
    else if (blocking && g_originalMatrixRotate)
    {
        const float bx = g_blockRotations[0].load(std::memory_order_relaxed);
        const float by = g_blockRotations[1].load(std::memory_order_relaxed);
        const float bz = g_blockRotations[2].load(std::memory_order_relaxed);

        g_originalMatrixRotate(matrix, bx, 1.0f, 0.0f, 0.0f);
        if (by != 0.0f)
            g_originalMatrixRotate(matrix, by, 0.0f, 1.0f, 0.0f);
        if (bz != 0.0f)
            g_originalMatrixRotate(matrix, bz, 0.0f, 0.0f, 1.0f);

        bool expected = false;
        if (g_blockFirstPersonLogged.compare_exchange_strong(expected, true))
            MC_LOG("[Animations] first person block pose applied (%.2f, %.2f, %.2f)",
                bx, by, bz);
    }
    else if (fresh && g_fluxSwing.load(std::memory_order_relaxed))
    {
        applyFluxDip(matrix, envelope,
            g_swingSquareFactor.load(std::memory_order_relaxed));
    }

    original(matrix, x * factor, y * factor, z * factor);
}

// Animation setup (sub_140321D00) runs right before the player model parts are
// drawn. The renderer keeps its VHumanoidModel at +0x248, so this is where the
// local model pointer is captured for the third person pose.
__int64 __fastcall animationSetupDetour(void* renderer, void* actor,
    float partialTick)
{
    const auto original = g_originalAnimationSetup;
    if (!original)
        return 0;

    if (actor && renderer &&
        reinterpret_cast<uintptr_t>(actor) == Game::get().localAddr())
    {
        const uintptr_t rendererAddress = reinterpret_cast<uintptr_t>(renderer);
        uintptr_t model = 0;
        uintptr_t vtable = 0;
        // The +0x248 slot belongs to the renderer of the actor that is being set
        // up, which was just checked to be the local player, so whatever lives
        // there is the local model. Only readability is validated: requiring an
        // exact VHumanoidModel vtable match silently skipped player model
        // subclasses, and the arm pose was then never applied.
        if (mem::rawRead(&model, rendererAddress + off::model::RendererModel,
                sizeof(model)) &&
            model && mem::rawRead(&vtable, model, sizeof(vtable)) && vtable &&
            mem::isReadable(model, off::model::HumanoidRightArm + 0x30))
        {
            rememberLocalModel(model);

            bool expected = false;
            if (g_modelCapturedLogged.compare_exchange_strong(expected, true))
                MC_LOG("[Animations] local model captured @ %p (vtable %p)",
                    reinterpret_cast<void*>(model),
                    reinterpret_cast<void*>(vtable));
        }
    }

    return original(renderer, actor, partialTick);
}

// ModelPart transform (sub_140227660) applies the radians stored at
// part+0x18/+0x1C/+0x20. While blocking, the local right arm triple is
// overwritten every time the part is transformed, so the guard is static and
// does not depend on the swing progress at all. The triple is left behind on
// purpose (no restore) so late consumers of the arm state still see the pose;
// the native setup pass (sub_140239940, invoked unconditionally by the model
// render) rewrites it at the start of the next frame, so nothing accumulates.
void __fastcall modelPartRenderDetour(void* part, void* model, float scale)
{
    const auto original = g_originalModelPartRender;
    if (!original)
        return;

    const uintptr_t modelAddress = reinterpret_cast<uintptr_t>(model);
    const uintptr_t partAddress = reinterpret_cast<uintptr_t>(part);
    if (!g_blockPoseActive.load(std::memory_order_relaxed) || !modelAddress ||
        !isLocalModel(modelAddress) ||
        partAddress != modelAddress + off::model::HumanoidRightArm)
    {
        original(part, model, scale);
        return;
    }

    const bool blocking = g_onlyOnBlock.load(std::memory_order_relaxed)
        ? isHoldingItem(true)
        : isHoldingItem(false);

    // Throttled state line so the log proves whether the arm is reached and
    // whether the block gate sees the button, even when nothing is drawn.
    static std::atomic<uint64_t> nextLogMs{0};
    const uint64_t nowMs = GetTickCount64();
    if (nowMs >= nextLogMs.load(std::memory_order_relaxed))
    {
        nextLogMs.store(nowMs + 2000, std::memory_order_relaxed);
        MC_LOG("[Animations] arm part reached @ model %p (blocking %d)",
            reinterpret_cast<void*>(modelAddress), blocking ? 1 : 0);
    }

    if (!blocking)
    {
        original(part, model, scale);
        return;
    }

    const uintptr_t rotationAddress =
        modelAddress + off::model::HumanoidRightArm + off::model::ModelPartRotation;
    const float rotation[3] = {
        deg2rad(g_blockRotations[0].load(std::memory_order_relaxed)),
        deg2rad(g_blockRotations[1].load(std::memory_order_relaxed)),
        deg2rad(g_blockRotations[2].load(std::memory_order_relaxed)),
    };
    if (!mem::rawWrite(rotationAddress, rotation, sizeof(rotation)))
    {
        original(part, model, scale);
        return;
    }

    bool expected = false;
    if (g_blockPoseLogged.compare_exchange_strong(expected, true))
        MC_LOG("[Animations] third person block pose applied @ model %p",
            reinterpret_cast<void*>(modelAddress));

    original(part, model, scale);
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
    if (!hook::create(target, reinterpret_cast<void*>(&swingDurationDetour),
            reinterpret_cast<void**>(&g_originalSwingDuration)))
    {
        MC_LOG_ERROR("[Animations] Failed to install swing duration hook");
        return false;
    }

    g_swingDurationTarget = target;
    MC_LOG("[Animations] swing duration hook installed @ %p", target);
    return true;
}

bool installMatrixRotateHook()
{
    if (g_swingRotateTarget)
        return true;

    const uintptr_t address = mem::resolve(off::fn::RenderMatrixRotate);
    const uintptr_t swingCallSite = mem::resolve(off::fn::ItemInHandSwingRotateReturn);
    const uintptr_t liftCallSite = mem::resolve(off::fn::ItemInHandSwingLiftReturn);
    const uintptr_t twistCallSite = mem::resolve(off::fn::ItemInHandSwingTwistReturn);
    if (!address || !swingCallSite || !liftCallSite || !twistCallSite ||
        !mem::isReadable(address, 16))
    {
        MC_LOG_ERROR("[Animations] Matrix rotate helper is unavailable");
        return false;
    }

    void* target = reinterpret_cast<void*>(address);
    if (!hook::create(target, reinterpret_cast<void*>(&matrixRotateDetour),
            reinterpret_cast<void**>(&g_originalMatrixRotate)))
    {
        MC_LOG_ERROR("[Animations] Failed to install matrix rotate hook");
        return false;
    }

    g_swingRotateReturn = swingCallSite;
    g_swingLiftReturn = liftCallSite;
    g_swingTwistReturn = twistCallSite;
    g_swingRotateTarget = target;
    MC_LOG("[Animations] matrix rotate hook installed @ %p (call sites X %p, Y %p, Z %p)",
        target, reinterpret_cast<void*>(swingCallSite),
        reinterpret_cast<void*>(liftCallSite),
        reinterpret_cast<void*>(twistCallSite));
    return true;
}

bool installMatrixScaleHook()
{
    if (g_swingScaleTarget)
        return true;

    const uintptr_t address = mem::resolve(off::fn::RenderMatrixScale);
    const uintptr_t callSite = mem::resolve(off::fn::ItemInHandSwingScaleReturn);
    if (!address || !callSite || !mem::isReadable(address, 16))
    {
        MC_LOG_ERROR("[Animations] Matrix scale helper is unavailable");
        return false;
    }

    void* target = reinterpret_cast<void*>(address);
    if (!hook::create(target, reinterpret_cast<void*>(&matrixScaleDetour),
            reinterpret_cast<void**>(&g_originalMatrixScale)))
    {
        MC_LOG_ERROR("[Animations] Failed to install matrix scale hook");
        return false;
    }

    g_swingScaleReturn = callSite;
    g_swingScaleTarget = target;

    // The translate helper is only called (never hooked): the preset table needs
    // it to place the guard exactly where Rise places it.
    const uintptr_t translateAddress = mem::resolve(off::fn::RenderMatrixTranslate);
    g_matrixTranslate = translateAddress && mem::isReadable(translateAddress, 16)
        ? reinterpret_cast<MatrixTranslateFn>(translateAddress)
        : nullptr;

    MC_LOG("[Animations] matrix scale hook installed @ %p (call site %p, translate %p)",
        target, reinterpret_cast<void*>(callSite),
        reinterpret_cast<void*>(translateAddress));
    return true;
}

bool installAnimationSetupHook()
{
    if (g_animationSetupTarget)
        return true;

    const uintptr_t address = mem::resolve(off::fn::HumanoidModelAnimationSetup);
    if (!address || !mem::isReadable(address, 16))
    {
        MC_LOG_ERROR("[Animations] Model animation setup is not readable");
        return false;
    }

    void* target = reinterpret_cast<void*>(address);
    if (!hook::create(target, reinterpret_cast<void*>(&animationSetupDetour),
            reinterpret_cast<void**>(&g_originalAnimationSetup)))
    {
        MC_LOG_ERROR("[Animations] Failed to install model animation setup hook");
        return false;
    }

    g_animationSetupTarget = target;
    MC_LOG("[Animations] model animation setup hook installed @ %p", target);
    return true;
}

bool installModelPartHook()
{
    if (g_modelPartTarget)
        return true;

    const uintptr_t address = mem::resolve(off::fn::ModelPartRender);
    if (!address || !mem::isReadable(address, 16))
    {
        MC_LOG_ERROR("[Animations] Model part renderer is not readable");
        return false;
    }

    void* target = reinterpret_cast<void*>(address);
    if (!hook::create(target, reinterpret_cast<void*>(&modelPartRenderDetour),
            reinterpret_cast<void**>(&g_originalModelPartRender)))
    {
        MC_LOG_ERROR("[Animations] Failed to install model part render hook");
        return false;
    }

    g_modelPartTarget = target;
    MC_LOG("[Animations] model part render hook installed @ %p", target);
    return true;
}

void publishFirstPersonSettings(bool moduleEnabled, bool customSwingAngle,
    bool fluxSwing, bool onlyOnBlock, float angle, bool smallItems,
    float smallItemScale)
{
    g_customSwingAngle.store(moduleEnabled && customSwingAngle && g_swingRotateTarget,
        std::memory_order_relaxed);
    g_fluxSwing.store(moduleEnabled && fluxSwing && g_swingRotateTarget,
        std::memory_order_relaxed);
    g_onlyOnBlockSwingAngle.store(onlyOnBlock, std::memory_order_relaxed);
    g_swingAngleDegrees.store(angle, std::memory_order_relaxed);
    g_smallItemsActive.store(moduleEnabled && smallItems && g_swingScaleTarget,
        std::memory_order_relaxed);
    g_smallItemsScale.store(smallItemScale, std::memory_order_relaxed);
}

void publishSwingTime(bool moduleEnabled, bool timeEnabled, int ticks)
{
    g_swingTime.store(std::clamp(ticks, 1, 20), std::memory_order_relaxed);
    g_swingTimeActive.store(moduleEnabled && timeEnabled && g_swingDurationTarget != nullptr,
        std::memory_order_relaxed);
}

void publishBlockSettings(bool moduleEnabled, bool onlyOnBlock, int preset, float x,
    float y, float z)
{
    // The pose is driven from three independent paths: the item scale call site
    // (first person, runs every frame), the swing rotation (suppressed while
    // blocking) and the right arm part (third person). Enable it as soon as any
    // of them is hooked so a single failed hook cannot disable the guard.
    g_blockPoseActive.store(
        moduleEnabled && (g_swingScaleTarget || g_swingRotateTarget || g_modelPartTarget),
        std::memory_order_relaxed);
    g_onlyOnBlock.store(onlyOnBlock, std::memory_order_relaxed);
    g_blockPreset.store(preset >= 0 && preset < kBlockPresetCount ? preset : 0,
        std::memory_order_relaxed);
    g_blockRotations[0].store(x, std::memory_order_relaxed);
    g_blockRotations[1].store(y, std::memory_order_relaxed);
    g_blockRotations[2].store(z, std::memory_order_relaxed);
}

}

Animations::Animations()
    : Module("Animations", "Customizes swing timing, item transforms and the sword block pose",
             Category::Visuals, 0)
{
    markHasSettings();
}

void Animations::onEnable()
{
    installSwingDurationHook();
    publishSwingTime(true, swingTimeEnabled_, swingTime_);
    installMatrixRotateHook();
    installMatrixScaleHook();
    installAnimationSetupHook();
    installModelPartHook();
    publishFirstPersonSettings(true, customSwingAngle_, fluxSwing_,
        onlyOnBlockSwingAngle_, swingAngle_, smallItems_, smallItemsScale_);
    publishBlockSettings(swordBlock_, onlyOnBlock_, blockPreset_, blockXRotation_,
        blockYRotation_, blockZRotation_);

    g_blockPoseLogged.store(false, std::memory_order_relaxed);
    g_blockFirstPersonLogged.store(false, std::memory_order_relaxed);
    g_blockPresetLogged.store(false, std::memory_order_relaxed);
    g_modelCapturedLogged.store(false, std::memory_order_relaxed);
    g_fluxLogCount.store(0, std::memory_order_relaxed);
}

void Animations::onDisable()
{
    g_swingTimeActive.store(false, std::memory_order_relaxed);
    publishFirstPersonSettings(false, customSwingAngle_, fluxSwing_,
        onlyOnBlockSwingAngle_, swingAngle_, smallItems_, smallItemsScale_);
    g_blockPoseActive.store(false, std::memory_order_relaxed);
    g_swingEnvelope.store(0.0f, std::memory_order_relaxed);
    g_swingSquareFactor.store(0.0f, std::memory_order_relaxed);
    g_swingEnvelopeStamp.store(0, std::memory_order_relaxed);
    forgetLocalModels();
}

void Animations::onShutdown()
{
    g_swingTimeActive.store(false, std::memory_order_relaxed);
    publishFirstPersonSettings(false, customSwingAngle_, fluxSwing_,
        onlyOnBlockSwingAngle_, swingAngle_, smallItems_, smallItemsScale_);
    g_blockPoseActive.store(false, std::memory_order_relaxed);
    forgetLocalModels();

    if (g_swingDurationTarget)
    {
        hook::remove(g_swingDurationTarget);
        g_swingDurationTarget = nullptr;
        g_originalSwingDuration = nullptr;
    }
    if (g_swingRotateTarget)
    {
        hook::remove(g_swingRotateTarget);
        g_swingRotateTarget = nullptr;
        g_originalMatrixRotate = nullptr;
    }
    if (g_swingScaleTarget)
    {
        hook::remove(g_swingScaleTarget);
        g_swingScaleTarget = nullptr;
        g_originalMatrixScale = nullptr;
        g_matrixTranslate = nullptr;
    }
    if (g_animationSetupTarget)
    {
        hook::remove(g_animationSetupTarget);
        g_animationSetupTarget = nullptr;
        g_originalAnimationSetup = nullptr;
    }
    if (g_modelPartTarget)
    {
        hook::remove(g_modelPartTarget);
        g_modelPartTarget = nullptr;
        g_originalModelPartRender = nullptr;
    }
}

void Animations::drawSettings()
{
    if (widgets::Checkbox("Swing Time##anim", &swingTimeEnabled_))
    {
        installSwingDurationHook();
        publishSwingTime(enabled(), swingTimeEnabled_, swingTime_);
    }
    if (swingTimeEnabled_)
    {
        if (widgets::CSlider("Swing Length##anim", &swingTime_, 1, 20, "%d ticks"))
            publishSwingTime(enabled(), swingTimeEnabled_, swingTime_);
    }

    if (widgets::Checkbox("Custom Swing Angle##anim", &customSwingAngle_))
        publishFirstPersonSettings(enabled(), customSwingAngle_, fluxSwing_,
            onlyOnBlockSwingAngle_, swingAngle_, smallItems_, smallItemsScale_);
    if (customSwingAngle_)
    {
        if (widgets::Checkbox("Only on Block##swing-angle", &onlyOnBlockSwingAngle_))
            publishFirstPersonSettings(enabled(), customSwingAngle_, fluxSwing_,
                onlyOnBlockSwingAngle_, swingAngle_, smallItems_, smallItemsScale_);
        if (widgets::CSlider("Swing Angle##anim", &swingAngle_, -180.0f, 180.0f, "%.2f deg"))
            publishFirstPersonSettings(enabled(), customSwingAngle_, fluxSwing_,
                onlyOnBlockSwingAngle_, swingAngle_, smallItems_, smallItemsScale_);
    }

    if (widgets::Checkbox("Flux Swing##anim", &fluxSwing_))
        publishFirstPersonSettings(enabled(), customSwingAngle_, fluxSwing_,
            onlyOnBlockSwingAngle_, swingAngle_, smallItems_, smallItemsScale_);

    if (widgets::Checkbox("Small Items##anim", &smallItems_))
        publishFirstPersonSettings(enabled(), customSwingAngle_, fluxSwing_,
            onlyOnBlockSwingAngle_, swingAngle_, smallItems_, smallItemsScale_);
    if (smallItems_)
    {
        if (widgets::CSlider("Item Size##anim", &smallItemsScale_, 0.2f, 1.0f, "%.2f"))
            publishFirstPersonSettings(enabled(), customSwingAngle_, fluxSwing_,
                onlyOnBlockSwingAngle_, swingAngle_, smallItems_, smallItemsScale_);
    }

    if (widgets::Checkbox("Sword Block##anim", &swordBlock_))
        publishBlockSettings(enabled() && swordBlock_, onlyOnBlock_, blockPreset_,
            blockXRotation_, blockYRotation_, blockZRotation_);
    if (swordBlock_)
    {
        if (widgets::Checkbox("Only on Block##anim", &onlyOnBlock_))
            publishBlockSettings(enabled() && swordBlock_, onlyOnBlock_, blockPreset_,
                blockXRotation_, blockYRotation_, blockZRotation_);

        // Rise's "Block Animation" list. Custom uses the three sliders below
        // (and is the only one the third person arm pose can follow, since that
        // path overrides a rotation triple instead of composing a matrix).
        static const std::vector<std::string> presetNames = [] {
            std::vector<std::string> names;
            names.reserve(kBlockPresetCount);
            for (const BlockPreset& preset : kBlockPresets)
                names.emplace_back(preset.name);
            return names;
        }();
        if (widgets::CCombo("Preset##anim", &blockPreset_, presetNames))
        {
            blockPreset_ = std::clamp(blockPreset_, 0, kBlockPresetCount - 1);
            g_blockPresetLogged.store(false, std::memory_order_relaxed);
            publishBlockSettings(enabled() && swordBlock_, onlyOnBlock_, blockPreset_,
                blockXRotation_, blockYRotation_, blockZRotation_);
        }

        if (blockPreset_ == 0)
        {
            const bool rotationChanged =
                widgets::CSlider("X Rotation##anim", &blockXRotation_, -180.0f, 180.0f, "%.2f deg") |
                widgets::CSlider("Y Rotation##anim", &blockYRotation_, -180.0f, 180.0f, "%.2f deg") |
                widgets::CSlider("Z Rotation##anim", &blockZRotation_, -180.0f, 180.0f, "%.2f deg");
            if (rotationChanged)
                publishBlockSettings(enabled() && swordBlock_, onlyOnBlock_, blockPreset_,
                    blockXRotation_, blockYRotation_, blockZRotation_);
        }
    }
}

std::string Animations::suffix() const
{
    if (swingTimeEnabled_)
        return std::to_string(swingTime_) + "t";
    if (blockPreset_ > 0 && blockPreset_ < kBlockPresetCount)
        return kBlockPresets[blockPreset_].name;
    return {};
}

nlohmann::json Animations::saveSettings() const
{
    return nlohmann::json{
        {"swingTimeEnabled", swingTimeEnabled_},
        {"swingTime", swingTime_},
        {"customSwingAngle", customSwingAngle_},
        {"fluxSwing", fluxSwing_},
        {"onlyOnBlockSwingAngle", onlyOnBlockSwingAngle_},
        {"swingAngle", swingAngle_},
        {"smallItems", smallItems_},
        {"smallItemsScale", smallItemsScale_},
        {"swordBlock", swordBlock_},
        {"onlyOnBlock", onlyOnBlock_},
        {"blockPreset", blockPreset_},
        {"blockXRotation", blockXRotation_},
        {"blockYRotation", blockYRotation_},
        {"blockZRotation", blockZRotation_},
    };
}

void Animations::loadSettings(const nlohmann::json& j)
{
    swingTimeEnabled_ = j.value("swingTimeEnabled", swingTimeEnabled_);
    swingTime_ = std::clamp(j.value("swingTime", swingTime_), 1, 20);
    customSwingAngle_ = j.value("customSwingAngle", customSwingAngle_);
    fluxSwing_ = j.value("fluxSwing", fluxSwing_);
    onlyOnBlockSwingAngle_ = j.value("onlyOnBlockSwingAngle", onlyOnBlockSwingAngle_);
    swingAngle_ = std::clamp(j.value("swingAngle", swingAngle_), -180.0f, 180.0f);
    smallItems_ = j.value("smallItems", smallItems_);
    smallItemsScale_ = std::clamp(j.value("smallItemsScale", smallItemsScale_), 0.2f, 1.0f);
    swordBlock_ = j.value("swordBlock", j.value("thirdPersonBlock", swordBlock_));
    onlyOnBlock_ = j.value("onlyOnBlock", onlyOnBlock_);
    blockPreset_ = std::clamp(j.value("blockPreset", blockPreset_), 0, kBlockPresetCount - 1);
    blockXRotation_ = std::clamp(j.value("blockXRotation", blockXRotation_), -180.0f, 180.0f);
    blockYRotation_ = std::clamp(j.value("blockYRotation", blockYRotation_), -180.0f, 180.0f);
    blockZRotation_ = std::clamp(j.value("blockZRotation", blockZRotation_), -180.0f, 180.0f);
    if (enabled())
    {
        publishSwingTime(true, swingTimeEnabled_, swingTime_);
        publishFirstPersonSettings(true, customSwingAngle_, fluxSwing_,
            onlyOnBlockSwingAngle_, swingAngle_, smallItems_, smallItemsScale_);
        publishBlockSettings(swordBlock_, onlyOnBlock_, blockPreset_, blockXRotation_,
            blockYRotation_, blockZRotation_);
    }
}

MC_REGISTER_MODULE(Animations);

}
