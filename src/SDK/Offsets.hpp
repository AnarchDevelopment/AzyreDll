#pragma once

#include <cstdint>

namespace mc::off {

namespace player
{
    constexpr uintptr_t VelocityX = 0x30;
    constexpr uintptr_t VelocityY = 0x34;
    constexpr uintptr_t VelocityZ = 0x38;
    constexpr uintptr_t Pitch = 0x3C;
    constexpr uintptr_t Yaw = 0x40;
    constexpr uintptr_t OnGround = 0x17D;
    constexpr uintptr_t Height = 0xD4;
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
    constexpr int MagicValue = 0x10040;
}

namespace useItemPacket
{
    constexpr uintptr_t Type = 0x08;
    constexpr uintptr_t Field0C = 0x0C;
    constexpr uintptr_t Vtable = 0x10;
    constexpr uintptr_t X = 0x14;
    constexpr uintptr_t Y = 0x18;
    constexpr uintptr_t Z = 0x1C;
    constexpr uintptr_t Face = 0x1E;
    constexpr uintptr_t ClickX = 0x20;
    constexpr uintptr_t ClickY = 0x28;
    constexpr uintptr_t PlayerX = 0x2C;
    constexpr uintptr_t PlayerY = 0x34;
    constexpr uintptr_t Flag = 0x58;
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
    constexpr uintptr_t RendererDimension = 0x1E0;
    constexpr uintptr_t DimensionLevel = 0x48;
    constexpr uintptr_t RendererCamera = 0x2548;
    constexpr uintptr_t ClientChat = 0x2B0;
    constexpr uintptr_t ChatMessages = 0x128;
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

namespace fn
{
    constexpr uintptr_t MinecraftClientTick = 0x04B360;

    constexpr uintptr_t ActorHurt = 0x4D5730;
    constexpr uintptr_t ActorActuallyHurt = 0x267DB0;

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

    // MouseDevice::feed(this, actionButtonId, buttonData, x, y, dx, dy,
    // forceMotionlessPointer): la entrada universal de input del juego.
    // actionButtonId 4 = wheel (0x78/0x7F arriba, 0x88/0x80 abajo).
    // RVA pendiente desde IDA (firma de Solstice no matchea este build).
    constexpr uintptr_t MouseDeviceFeed = 0;
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

}
