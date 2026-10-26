#pragma once

#include <cstddef>
#include <cstdint>

namespace mc::off {

namespace player
{
    // Runtime ID del Actor, consumido por VAnimatePacket y VInteractPacket.
    constexpr uintptr_t RuntimeID = 0x198;
    // Posicion REAL (mPosition): la escribe Actor::setPos (sub_1404B27B0).
    constexpr uintptr_t PosTrueX = 0x0C;
    constexpr uintptr_t PosTrueY = 0x10;
    constexpr uintptr_t PosTrueZ = 0x14;
    // AABB del actor: min en 0xB4 (lo que leiamos como "pos"), max en 0xC0.
    // setPos: halfW = *(0xD0)*0.5; ymin = pos.y - 0xE4 + 0xEC; ymax = ymin + 0xD4.
    constexpr uintptr_t AabbMinX = 0xB4;
    constexpr uintptr_t AabbMinY = 0xB8;
    constexpr uintptr_t AabbMinZ = 0xBC;
    constexpr uintptr_t AabbMaxX = 0xC0;
    constexpr uintptr_t AabbMaxY = 0xC4;
    constexpr uintptr_t AabbMaxZ = 0xC8;
    constexpr uintptr_t AabbIsZero = 0xCC;
    constexpr uintptr_t AabbWidth = 0xD0;
    constexpr uintptr_t AabbHeight = 0xD4;
    constexpr uintptr_t AabbYAdjA = 0xE4;
    constexpr uintptr_t AabbYAdjB = 0xEC;
    // Target de red ("moveTo", slot 15 = sub_1404D4FB0): pos + rot + flags.
    // DECOMPILADO — el layout REAL de moveTo es:
    //   *(float*)(a1 + 0xD08..0xD10) = a2 (Vec3 pos, 3 floats)
    //   *(float*)(a1 + 0xD14) = *(float*)a3       // rot.x  = PITCH
    //   *(float*)(a1 + 0xD18) = *((float*)a3 + 1) // rot.y  = YAW/HEADDYAW
    //   *(int*)  (a1 + 0xD1C) = a4 (flags)
    // a3 es el Vec2& rot del MovePlayerPacket (+0x24 pitch, +0x28 yaw).
    // NOTA: en el paquete hay TRES rotaciones (+0x24 pitch, +0x28 yaw,
    // +0x2C headYaw), pero moveTo solo guarda DOS — no copia el headYaw
    // separado. Ver off::actor::NetworkYaw/NetworkHeadYaw abajo.
    constexpr uintptr_t MoveTargetX = 0xD08;
    constexpr uintptr_t MoveTargetY = 0xD0C;
    constexpr uintptr_t MoveTargetZ = 0xD10;
    // Red rot (CONFIRMADO por decompile de Actor::moveTo = sub_1404D4FB0,
    // slot 14). moveTo escribe el Vec2& rot del paquete en DOS floats
    // consecutivos: +0xD14 = pitch, +0xD18 = yaw. Es la PRIMERA separacion
    // fisica pitch/yaw que tenemos del actor. El paquete trae un TERCER
    // float (headYaw, +0x2C) que moveTo NO guarda — no hay campo de
    // headYaw en esta cadena.
    constexpr uintptr_t NetworkPitch = 0xD14;
    constexpr uintptr_t NetworkYaw = 0xD18;
    constexpr uintptr_t MoveTargetFlags = 0xD1C;
    constexpr uintptr_t VelocityX = 0x30;
    constexpr uintptr_t VelocityY = 0x34;
    constexpr uintptr_t VelocityZ = 0x38;
    constexpr uintptr_t Pitch = 0x3C;
    constexpr uintptr_t Yaw = 0x40;
    // Render rot (confirmado por MPPDump: duplican al pitch/yaw cada
    // frame): es la rotacion que usa el RENDER del modelo. Escribir SOLO
    // estos gira el modelo en 2ª/3ª persona sin tocar la camara (que
    // lee +0x3C/+0x40).
    constexpr uintptr_t RenderPitch = 0x44;
    constexpr uintptr_t RenderYaw = 0x48;

    // BodyRot from Actor serialization; both values set to the same angle
    // move the local player's body in the in-game test.
    constexpr uintptr_t BodyYawField = 0x210;
    constexpr uintptr_t BodyYawPreviousField = 0x214;
    // Head yaw del Actor: el ctor de MovePlayerPacket (sub_140263340) copia
    // [Actor+0x218] directamente a [Packet+0x2C] (HeadYaw, confirmado por MPPDump).
    constexpr uintptr_t HeadYaw = 0x218;
    // Previous-tick head yaw read by PlayerRenderer alongside HeadYaw.
    constexpr uintptr_t HeadYawPreviousField = 0x21C;

    // BODY ROT (0x0C) — CONFIRMADO por decompile de sub_1404D4560 (vtable
    // LocalPlayer slot 211). El orquestador lee `*((_DWORD *)a1 + 3)` =
    // +0x0C (X de mPosition) como yaw y `*((_DWORD *)a1 + 5)` = +0x14 (Z
    // de mPosition) como pitch, arma dos Vec3 y llama a sub_1405EAE30.
    // O sea: el yaw de RENDER del modelo ES el X de mPosition (+0x0C).
    // Escribir SOLO +0x0C gira el modelo sin tocar +0x3C/+0x40 (camara).
    constexpr uintptr_t BodyYaw = 0x0C;
    // +0x14 es el Z de mPosition y el orquestador lo usa como pitch de
    // render. Es el MISMO caso que BodyYaw pero en el eje vertical.
    constexpr uintptr_t RenderPitchAlt = 0x14;
    // Zona que cubre los campos de render del orquestador (+0x0C..+0x18):
    // 3 floats leidos como x/y/z del "render pos". El yaw va en +0x0C y
    // el pitch en +0x14; +0x10 no se uso en el decompile.
    constexpr uintptr_t RenderRotBegin = 0x0C;
    constexpr uintptr_t RenderRotEnd = 0x18;
    constexpr uintptr_t OnGround = 0x17D;
    constexpr uintptr_t Height = 0xD4;
    // Integer cache copied from the generic.health AttributeInstance by the
    // native hurt handler. This is current health, never maximum health.
    constexpr uintptr_t Health = 0x220;
    constexpr uintptr_t LevelPtr = 0x90;
    constexpr uintptr_t PosX = 0xB4;
    constexpr uintptr_t PosY = 0xB8;
    constexpr uintptr_t PosZ = 0xBC;
    constexpr uintptr_t Name = 0xE10;
    constexpr uintptr_t BlockSourceAlt = 0x1F0;
    constexpr uintptr_t InventoryPtr = 0xF38;
    constexpr uintptr_t MinecraftClientPtr = 0x1020;
    constexpr uintptr_t LeftClickFlag = 0x105D;
    constexpr uintptr_t HotbarSlot = 0x10C0;
    constexpr uintptr_t MoveInputPtr = 0x1178;
    constexpr uintptr_t IsHurt = 0x0CD4;
    constexpr uintptr_t HurtTime = 0x0CD8;
    constexpr uintptr_t IsUsingItem = 0x08F8;
    constexpr uintptr_t UseItemRemainingTicks = 0x0F58;
    constexpr uintptr_t UseItemDuration = 0x0F6C;
    constexpr uintptr_t TicksUsingItem = 0x0F78;
    // Skin descriptor consumed by the player-renderer path (sub_1402DE840).
    constexpr uintptr_t SkinInfo = 0x0F40;
}

namespace skin
{
    constexpr uintptr_t ServerPixelsBegin = 0x78;
    constexpr uintptr_t ServerPixelsEnd = 0x80;
    constexpr uintptr_t ClientImage = 0x78;
    constexpr uintptr_t ImagePixelsBegin = 0x40;
    constexpr uintptr_t ImagePixelsEnd = 0x48;
    constexpr size_t PixelBytes = 64u * 64u * 4u;
}

namespace inv
{
    constexpr uintptr_t ItemsBegin = 0x78;
    constexpr uintptr_t ItemsEnd = 0x80;
    constexpr uintptr_t HotbarMapBegin = 0x98;
    constexpr uintptr_t HotbarMapEnd = 0xA0;
    constexpr uintptr_t SelectedSlot = 0xB0;
}

namespace item
{
    constexpr uintptr_t Magic0 = 0x00;
    constexpr uintptr_t Count1 = 0x04;
    constexpr uintptr_t Count2 = 0x08;
    constexpr uintptr_t ItemPtr = 0x10;
    constexpr uintptr_t Count = 0x18;
    constexpr uintptr_t Metadata = 0x20;
    // Item::mLegacyId. The ItemStack network serializer sub_1403960E0 reads
    // a signed short from Item* + 0x36 before serializing its numeric ID.
    constexpr uintptr_t LegacyId = 0x36;
    constexpr int MagicValue = 0x10040;
}

namespace useItemPacket
{
    // Layout REAL confirmado por los decompiles del ctor (sub_1405100F0) y
    // de su caller (sub_1405113F0 = GameMode::useItemOn, vtable slot 12):
    //   +0x00 vtable, +0x08 Type=2, +0x0C Field=1, +0x10 X, +0x14 Y,
    //   +0x18 Z, +0x1C Face(byte), +0x20 PLAYERPOS Vec3 (param 5 del ctor),
    //   +0x2C CLICK Vec3 RELATIVO al bloque (param 6 — el caller calcula
    //   clickAbs - blockPos), +0x38 ItemStack copiado, +0x58 Flag = el
    //   HOTBAR SLOT seleccionado (supplies+176 = off::inv::SelectedSlot).
    // (La version anterior tenia Click/Player intercambiados: corregido.)
    constexpr uintptr_t Type = 0x08;
    constexpr uintptr_t Field0C = 0x0C;
    constexpr uintptr_t X = 0x10;
    constexpr uintptr_t Y = 0x14;
    constexpr uintptr_t Z = 0x18;
    constexpr uintptr_t Face = 0x1C;
    constexpr uintptr_t PlayerX = 0x20;
    constexpr uintptr_t PlayerY = 0x24;
    constexpr uintptr_t PlayerZ = 0x28;
    constexpr uintptr_t ClickX = 0x2C;
    constexpr uintptr_t ClickY = 0x30;
    constexpr uintptr_t ClickZ = 0x34;
    constexpr uintptr_t Item = 0x38;
    constexpr uintptr_t Flag = 0x58;
    constexpr int MagicValue = 0x10040;
}

namespace movePlayerPacket
{
    // MovePlayerPacket (vtable 0xE644F8) — el paquete de movimiento del
    // cliente pre-1.16: posicion + rotacion (pitch/yaw/headYaw) + flags.
    // El equivalente del PlayerAuthInputPacket que intercepta Solstice
    // para las silent rotations.
    // OFFSETS TENTATIVOS (patron pre-1.16 + layout de este build): los
    // datos arrancan en +0x08 tras la vtable. CONFIRMAR con:
    //   1) [PktTrace] MovePlayer dump (Trace 30s + mover la camara: los
    //      floats que cambian = pitch/yaw/headYaw), o
    //   2) decompile del ctor (xrefs a vtable 0x140E644F8 — igual que
    //      sub_1405100F0 para el UseItemPacket).
    // Layout REAL confirmado por el [MPPDump] (sesion 12:34, build con
    // auto-dump): los floats +24/+28/+2C coinciden 1:1 con el
    // pitch/yaw/headYaw del actor en 4 capturas consecutivas, y
    // +18/+1C/+20 son la POSICION (constantes con el player parado).
    //   +08=entityID(2) +18=(x,y,z) +24=Pitch +28=Yaw +2C=HeadYaw
    //   +30=mode/flags(16.00)
    constexpr uintptr_t RuntimeID = 0x08;
    constexpr uintptr_t X = 0x18;
    constexpr uintptr_t Y = 0x1C;
    constexpr uintptr_t Z = 0x20;
    constexpr uintptr_t Pitch = 0x24;
    constexpr uintptr_t Yaw = 0x28;
    constexpr uintptr_t HeadYaw = 0x2C;
    constexpr uintptr_t Mode = 0x30;

    // TRUE: el [MPPDump] confirmo el layout. El hook del MovePlayerPacket
    // escribe SOLO pitch/yaw/headYaw (la posicion queda intacta).
    constexpr bool OffsetsConfirmed = true;
}

namespace interactPacket
{
    // Layout de VInteractPacket (constructor natural sub_140397A20 y
    // serializer sub_140043B40): vtable, type=2, field=1, action byte,
    // padding, target runtime ID.
    constexpr uintptr_t Type = 0x08;
    constexpr uintptr_t Field = 0x0C;
    constexpr uintptr_t Action = 0x10;
    constexpr uintptr_t TargetRuntimeID = 0x18;
    constexpr uint8_t AttackAction = 2; // clic izquierdo sobre entidad
}

namespace textPacket
{
    constexpr uintptr_t Type = 0x08;
    constexpr uintptr_t SourceName = 0x18;
    constexpr uintptr_t Message = 0x38;
    constexpr uintptr_t MessageSize = 0x48;
    constexpr uintptr_t MessageCap = 0x50;
}

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
    // MinecraftClient -> NetworkConnector (+0xE8) -> PacketSender (+0x98).
    // Confirmed from the native entity left-click path (sub_14004B9D0).
    constexpr uintptr_t ClientNetworkConnector = 0xE8;
    constexpr uintptr_t NetworkConnectorPacketSender = 0x98;
    constexpr uintptr_t RendererDimension = 0x1E0;
    constexpr uintptr_t DimensionLevel = 0x48;
    constexpr uintptr_t RendererCamera = 0x2548;
    constexpr uintptr_t ClientChat = 0x2B0;
    constexpr uintptr_t ChatMessages = 0x128;

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

// Efectos activos: ScreenContext (cacheado por el hook del render) ->
// dueno del vector en +0x290 -> vector de 16 bytes por entrada
// (id:+0, duration ticks:+4, amplifier:+8). Mismos offsets que el prototipo.
namespace effects
{
    constexpr uintptr_t CtxOwner = 0x290;
    constexpr uintptr_t VectorBegin = 0xCB8;
    constexpr uintptr_t VectorEnd = 0xCC0;
}

namespace damage
{
    constexpr uintptr_t Vtable = 0x00;
    constexpr uintptr_t DsType = 0x08;
    constexpr uintptr_t Attacker = 0x10;
    constexpr uintptr_t AttackerVariant = 0x28;
    constexpr uintptr_t Victim = 0x30;
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
}

namespace fn
{
    constexpr uintptr_t MinecraftClientTick = 0x04B360;

    constexpr uintptr_t ActorHurt = 0x4D5730;
    constexpr uintptr_t ActorActuallyHurt = 0x267DB0;
    // Actor animation entry that emits the normal AnimatePacket (action 1).
    // Confirmed in the current Minecraft.Win10.DX11.exe build via REA.
    constexpr uintptr_t ActorSwing = 0x268420;
    // Called by ActorSwing with the Actor* and used as the current swing
    // duration (the caller compares swing progress against return_value / 2).
    // This is based on the current executable's ActorSwing decompile.
    constexpr uintptr_t ActorGetSwingDuration = 0x4DA4A0;

    constexpr uintptr_t UseItemPacketHandle = 0x394CB0;
    constexpr uintptr_t UseItemPacketRead = 0x394CC0;
    constexpr uintptr_t UseItemPacketWrite = 0x394DB0;
    constexpr uintptr_t UseItemPacketGetName = 0x394E90;
    constexpr uintptr_t UseItemPacketCtor = 0x5100F0;
    constexpr uintptr_t UseItemPacketCtorVariant = 0x510270;

    constexpr uintptr_t BlockItemUseOn = 0x5201F0;

    constexpr uintptr_t GameModeUseItemOnWrapper = 0x5113F0;
    constexpr uintptr_t InputWrapper1 = 0x5111F0;
    constexpr uintptr_t InputWrapper2 = 0x511270;
    constexpr uintptr_t ItemInstanceUpdate = 0x55D960;
    // ItemStack copy constructor used by LocalPlayer's native
    // MobEquipmentPacket synchronization path.
    constexpr uintptr_t ItemStackCopy = 0x55D350;

    constexpr uintptr_t BlockSourceGetBlock = 0x5E73F0;
    constexpr uintptr_t BlockSourceMayPlace = 0x5E8120;
    constexpr uintptr_t Vec3ToBlockPos = 0x5E6A70;
    constexpr uintptr_t BlockSourceHelper = 0x5E8B50;
    constexpr uintptr_t BlockSourceDestructor = 0x5E6C20;
    constexpr uintptr_t BlockSourceConstructor = 0x5E6B20;

    // (this, NetworkIdentifier*, Packet*)
    constexpr uintptr_t SendToServer = 0x38B940;

    // ChatCtrl::addMessage(this, const std::string&, const std::string&, int, char)
    constexpr uintptr_t ChatAddMessage = 0x0C5C00;

    // Render que recibe el ScreenContext* como segundo arg (firma completa
    // desconocida: el thunk de SDK/Effects.cpp solo roba rdx sin tocar el
    // resto). Es el mismo que usa el prototipo de potion_hud.py.
    constexpr uintptr_t RenderWithScreenContext = 0x090B60;

    // PlayerRenderer model setup (sub_1402D93F0), verified in the current
    // build to interpolate Actor body/head yaw pairs into model state.
    constexpr uintptr_t PlayerRendererModelSetup = 0x2D93F0;

    // Humanoid model animation setup (sub_140321D00), called from the player
    // renderer before the model parts are drawn.
    constexpr uintptr_t HumanoidModelAnimationSetup = 0x321D00;

    // Primera persona: composición de pose del item y preparación final del draw.
    // REA: sub_1402FB690 llama a sub_1402FA2F0 en 0x1402FC0B3.
    constexpr uintptr_t ItemInHandRender = 0x2FB690;
    constexpr uintptr_t ItemInHandDraw = 0x2FA2F0;
    constexpr uintptr_t ItemInHandDrawReturn = 0x2FC0B8;
    constexpr uintptr_t ItemInHandSwingTransform = 0x2FC5A0;
    constexpr uintptr_t ItemInHandSwingTransformReturn = 0x2FB955;

    // Matriz stack/render helpers confirmados con REA en este build.
    constexpr uintptr_t RenderMatrixStackPush = 0x793F40;
    constexpr uintptr_t RenderMatrixTranslate = 0x067630;
    constexpr uintptr_t RenderMatrixRotate = 0x088410;

    // MouseDevice::feed(this, actionButtonId, buttonData, x, y, dx, dy,
    // forceMotionlessPointer): la entrada universal de input del juego.
    // actionButtonId 4 = wheel (0x78/0x7F arriba, 0x88/0x80 abajo).
    // RVA pendiente desde IDA (firma de Solstice no matchea este build).
    constexpr uintptr_t MouseDeviceFeed = 0;

    // Actor::moveTo (sub_1404D4FB0, slot 15 de la vtable, +120): el store
    // del target de red (+0xD08 pos, +0xD14 rot, +0xD1C flags) que escriben
    // los handlers de MoveEntityPacket/MovePlayerPacket. Firma:
    //   void moveTo(this, const Vec3& pos, const Vec2& rot, int flags)
    // Confirmado por decompilacion (IDA) en el build actual.
    constexpr uintptr_t ActorMoveTo = 0x4D4FB0;

    // GameMode::buildBlock(BlockPos const& baseCell, uchar blockSide, bool):
    // LA entrada real de colocacion del cliente (arquitectura Solstice):
    // lee el HitResult del crosshair, aplica local y arma+envia el
    // UseItemPacket. Hallar con xrefs a los ctors inline de UseItemPacket
    // (0x5100F0 / 0x510270): el caller que construye el paquete ES
    // buildBlock. RVA pendiente del IDA.
    constexpr uintptr_t GameModeBuildBlock = 0;
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

namespace blocks
{
    constexpr int Air = 0;
    constexpr int Stone = 1;
    constexpr int Grass = 2;
    constexpr int Dirt = 3;
    constexpr int Cobblestone = 4;
    constexpr int Planks = 5;
    constexpr int Bedrock = 7;
    constexpr int Water = 8;
    constexpr int Lava = 10;
    constexpr int Sand = 12;
    constexpr int Gravel = 13;
    constexpr int GoldOre = 14;
    constexpr int IronOre = 15;
    constexpr int CoalOre = 16;
    constexpr int Log = 17;
    constexpr int Leaves = 18;
    constexpr int LapisOre = 21;
    constexpr int Sandstone = 24;
    constexpr int Chest = 54;
    constexpr int DiamondOre = 56;
    constexpr int RedstoneOre = 73;
    constexpr int EmeraldOre = 129;
    constexpr int EnderChest = 130;
    constexpr int AncientDebris = 526;
    constexpr int CopperOre = 566;
}

namespace dstype
{
    constexpr int Contact = 1;
    constexpr int EntityAttack = 2;
    constexpr int Projectile = 3;
    constexpr int Suffocation = 4;
    constexpr int Fall = 5;
    constexpr int Fire = 6;
    constexpr int OnFire = 7;
    constexpr int Lava = 8;
    constexpr int Drowning = 9;
    constexpr int BlockExplosion = 10;
    constexpr int EntityExplosion = 11;
    constexpr int Void = 12;
    constexpr int Starvation = 13;
    constexpr int Magic = 14;
    constexpr int Cactus = 15;
    constexpr int Wither = 16;
    constexpr int Anvil = 17;
    constexpr int Thorns = 18;
    constexpr int FallingBlock = 19;
    constexpr int Fireworks = 20;
    constexpr int FallingStalactite = 21;
    constexpr int Trident = 22;
    constexpr int Magma = 23;
    constexpr int Campfire = 24;
    constexpr int SoulCampfire = 25;
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

// ---- Lectura de bloques e items -------------------------------------------
// El objeto Block* (retorno de BlockSource::getBlock, RVA 0x5E73F0) no tiene
// layout confirmado en este build: se prueban varios offsets candidatos y se
// acepta el primer int con valor plausible. Ajustar en IDA si ningun candidato
// acierta (basta con poner el correcto primero).
namespace block
{
    inline constexpr uintptr_t IdCandidates[] = {
        0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24,
        0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C};
    inline constexpr int IdMin = 0;
    inline constexpr int IdMax = 4096;
}

// El Item* (ItemInstance::item()) tampoco tiene el offset del id confirmado.
namespace item
{
    inline constexpr uintptr_t IdCandidates[] = {0x08, 0x0C, 0x10, 0x14,
                                                 0x18, 0x1C, 0x20, 0x24};
    inline constexpr int IdMin = 0;
    inline constexpr int IdMax = 4096;
}

// ---- Lectura de bloques por chunks (LevelChunk) ---------------------------
// Cadena: BlockSource +0x18 -> ChunkSource +0x20 -> MainChunkSource +0x38
// (std::unordered_map<ChunkPos, LevelChunk*>).  La lista se recorre por los
// nodos (_Myhead, _Next).  El array de bloques del LevelChunk es 16x16x128
// bytes (1 byte por bloque) con indice (z_local + 16*x_local)*128 + y.
namespace chunk
{
    constexpr uintptr_t BlockSourceChunkSource = 0x18; // BlockSource -> ChunkSource
    constexpr uintptr_t ChunkSourceMain = 0x20;        // ChunkSource -> MainChunkSource
    constexpr uintptr_t MainChunkSourceHashMap = 0x38; // hash_map de chunks

    // std::unordered_map (layout de este build)
    constexpr uintptr_t HashMapHead = 0x00; // _Myhead (nodo centinela)
    constexpr uintptr_t HashMapSize = 0x08; // _Mysize (numero de chunks)

    // Nodo de la lista
    constexpr uintptr_t NodeNext = 0x00;  // _Next
    constexpr uintptr_t NodeKeyX = 0x10;  // chunkX (key)
    constexpr uintptr_t NodeKeyZ = 0x14;  // chunkZ (key)
    constexpr uintptr_t NodeValue = 0x20; // LevelChunk*

    // LevelChunk
    constexpr uintptr_t X = 0x28;      // chunkX
    constexpr uintptr_t Z = 0x2C;      // chunkZ
    constexpr uintptr_t State = 0x80;  // state (>= 2 = cargado)
    constexpr uintptr_t Blocks = 2704; // 0xA90 — u8[32768] (Y 0..127)

    constexpr int BoundsXZ = 16;
    constexpr int Height = 128;
    constexpr int Volume = 16 * 16 * 128;
}

}
