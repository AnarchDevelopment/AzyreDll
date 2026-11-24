#pragma once

#include <cstddef>
#include <cstdint>

namespace mc::off {

// Client, camera, input and runtime utility layouts for the active game build.
namespace chain
{
    constexpr uintptr_t PlayerLevel = 0x90;
    constexpr uintptr_t LevelBlockSource = 0x198;
    constexpr uintptr_t LevelNetworkHandler = 0x1648;
    constexpr uintptr_t NetworkHandlerGameMode = 0x20;
    constexpr uintptr_t PlayerInventory = 0xF38;
    constexpr uintptr_t PlayerClient = 0x1020;
    constexpr uintptr_t PlayerMoveInput = 0x1178;
    constexpr uintptr_t PlayerBlockSourceAlt = 0x1F0;
    constexpr uintptr_t ClientLevelRenderer = 0xF0;
    constexpr uintptr_t RendererDimension = 0x1E0;
    constexpr uintptr_t DimensionLevel = 0x48;
    constexpr uintptr_t RendererCamera = 0x2548;
    constexpr uintptr_t ClientChat = 0x2B0;
    // ChatHelper: DOS std::vector<Message> de 112 bytes (REA sub_1400C5C00:
    // los dos bucles de recorte dividen por 112LL y limitan a 0x1E / 0xA).
    //   +0x128: scrollback (30 mensajes)
    //   +0x140: lista que pinta el HUD (10 mensajes)
    // El HUD lee la segunda, asi que ocultar el chat nativo exige vaciar AMBAS.
    constexpr uintptr_t ChatMessages = 0x128;
    constexpr uintptr_t ChatHudMessages = 0x140;

    // Level -> HitResult del crosshair (forja estilo Solstice placeBlock).
    // Si HITRESULT_IS_POINTER es 1 el campo es un HitResult* (mov); si 0
    // la struct vive embedida en el Level (lea) y el offset apunta directo.
    // Layout (Solstice 1.21, clase estable):
    //   +0x00 vec3 mStartPos, +0x0C vec3 mRayDir, +0x18 int mType,
    //   +0x1C uchar mFacing, +0x20 ivec3 mBlockPos, +0x2C vec3 mPos,
    //   +0x6C bool mIndirectHit.
    // Pendiente del decompile de buildBlock (como lee el hit).
    constexpr uintptr_t LevelHitResult = 0;
    constexpr int LevelHitResultIsPointer = 1;
}

namespace chat
{
    constexpr uintptr_t MessageSize = 112;
    constexpr uintptr_t MsgType = 0x00;
    constexpr uintptr_t MsgSourceName = 0x08;
    constexpr uintptr_t MsgVariant = 0x28;
    constexpr uintptr_t MsgText = 0x48;
}

namespace effects
{
    constexpr uintptr_t CtxOwner = 0x290;
    constexpr uintptr_t RendererContext = 0xD0;
    constexpr uintptr_t RenderContextActor = 0x290;
    constexpr uintptr_t VectorBegin = 0xCB8;
    constexpr uintptr_t VectorEnd = 0xCC0;
}

namespace input
{
    constexpr uintptr_t InnerHandler = 0x10;
    constexpr uintptr_t InnerClickCooldown = 0x278;
    constexpr uintptr_t InnerClickCounter = 0x274;
}

namespace model
{
    // VHumanoidModel constructor (sub_1402391E0) creates rightArm at
    // model+0x638. ModelPart rotation is at part+0x18; the constructor helper
    // sub_140227280 converts its degree-valued source rotations to radians.
    constexpr uintptr_t HumanoidRightArm = 0x638;
    constexpr uintptr_t ModelPartRotation = 0x18;
    constexpr uintptr_t HumanoidLeftArm = 0x730;
    // ModelPart layout (stride 0xF8): +0/+4/+8 offset in pixels,
    // +0x18/+0x1C/+0x20 rotation X/Y/Z in radians (sub_140227660 multiplies
    // them by 180/pi before building the part matrix).
    constexpr uintptr_t ModelPartOffsetX = 0x00;
    constexpr uintptr_t ModelPartRotationX = 0x18;
    constexpr uintptr_t ModelPartRotationY = 0x1C;
    constexpr uintptr_t ModelPartRotationZ = 0x20;
    constexpr uintptr_t ModelPartVisible = 0x25;
    constexpr uintptr_t ModelPartHidden = 0x70;
    // Player renderer field holding the VHumanoidModel it animates and draws
    // (sub_1402D93F0 stores swing progress and pose flags at model+0x1C/0x20/
    // 0x21 and renders it through vtable slot 9).
    constexpr uintptr_t RendererModel = 0x248;
}

namespace gameconst
{
    constexpr int Tps = 20;
    constexpr int TickDurationMs = 50;
    constexpr float BlockSize = 1.0f;
    constexpr float PlayerHeight = 1.8f;
    constexpr float PlayerWidth = 0.6f;
    constexpr float EyeHeight = 1.62f;
    constexpr float Gravity = 0.08f;
    constexpr float TerminalVelocity = 3.92f;
    constexpr float JumpVelocity = 0.42f;
    constexpr float WalkSpeedBlocksPerSec = 4.317f;
    constexpr float SprintSpeedBlocksPerSec = 5.612f;
}

namespace misc
{
    constexpr uintptr_t Reach = 0xB52A70;
    constexpr uintptr_t HitboxExpand = 0x4B57B0;
    constexpr uintptr_t GlideFallback = 0x4D740B;
}

namespace render
{
    // Global MatrixStack usada por el item renderer (xmmword_140FDD850).
    constexpr uintptr_t MatrixStack = 0xFDD850;
}

}
