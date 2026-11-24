#pragma once

#include <cstddef>
#include <cstdint>

namespace mc::off {

// Actor fields and actor-owned attribute layouts for the active game build.
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
    // Legacy integer health cache candidate. TargetHUD now reads the live
    // generic.health AttributeInstance through off::attribute instead.
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
    constexpr uintptr_t PreviousSwingProgress = 0x0DE8;
    constexpr uintptr_t SwingProgress = 0x0DEC;
    constexpr uintptr_t IsUsingItem = 0x08F8;
    constexpr uintptr_t UseItemRemainingTicks = 0x0F58;
    constexpr uintptr_t UseItemDuration = 0x0F6C;
    constexpr uintptr_t TicksUsingItem = 0x0F78;
    // Skin descriptor consumed by the player-renderer path (sub_1402DE840).
    constexpr uintptr_t SkinInfo = 0x0F40;
    constexpr uintptr_t NameStringData = 0x00;
    constexpr uintptr_t NameStringSize = 0x10;
    constexpr uintptr_t NameStringCapacity = 0x18;
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

namespace attribute
{
    // Actor vtable getter used by the native Actor tick for generic.health.
    // REA: sub_1404D4840 calls vslot +0x5C0 with this attribute descriptor.
    constexpr uintptr_t ActorAttributeGetterVtableOffset = 0x5C0;
    // AttributeInstance fields read by the native health getters:
    // sub_1404DCC00 (max health) reads +0x80; sub_1404DCBB0 (current health)
    // and Actor::tick read +0x84.
    constexpr uintptr_t InstanceCurrentMaxValue = 0x80;
    // Effective value read by Actor tick from the returned AttributeInstance.
    constexpr uintptr_t InstanceCurrentValue = 0x84;
    // generic.health descriptor initialized at dword_140FD5F58.
    constexpr uintptr_t GenericHealth = 0xFD5F58;
}

namespace damage
{
    constexpr uintptr_t Vtable = 0x00;
    constexpr uintptr_t DsType = 0x08;
    constexpr uintptr_t Attacker = 0x10;
    constexpr uintptr_t AttackerVariant = 0x28;
    constexpr uintptr_t Victim = 0x30;
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

}
