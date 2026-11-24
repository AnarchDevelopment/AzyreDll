#include "GameMode.hpp"

#include "Features/HitFeed.hpp"
#include "Framework/Hook.hpp"
#include "Framework/Log.hpp"
#include "SDK/UI/Hooks/ChatHook.hpp"
#include "SDK/Core/Game.hpp"
#include "SDK/Entity/Actor.hpp"
#include "SDK/Entity/LocalPlayer.hpp"
#include "SDK/Network/Packets/InteractPacket.hpp"
#include "SDK/Network/Packets/PacketSender.hpp"
#include "SDK/Network/Packets/UseItemPacket.hpp"

#include <cmath>
#include <excpt.h>

namespace mc {

namespace {

// REA: sub_1405103D0 returns the char from sub_140500320, or an early
// guard result. Preserve it for diagnostics; it is not a transport status.
using AttackFn = char(__fastcall*)(void* gameMode, void* player, void* target);

void destroyItemStackCopy(uintptr_t stackAddress)
{
    uintptr_t owner = 0;
    if (!mem::rawRead(&owner, stackAddress + sizeof(uintptr_t), sizeof(owner)) ||
        !owner || !mem::isReadable(owner, sizeof(uintptr_t)))
        return;

    const uintptr_t ownerVtable = mem::read<uintptr_t>(owner);
    if (!ownerVtable || !mem::isReadable(ownerVtable, sizeof(uintptr_t)))
        return;
    const uintptr_t destroy = mem::read<uintptr_t>(ownerVtable);
    if (!destroy)
        return;

    __try
    {
        using DestroyFn = void(__fastcall*)(void*, int);
        reinterpret_cast<DestroyFn>(destroy)(reinterpret_cast<void*>(owner), 1);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

char callAttackFn(uintptr_t fnAddr, uintptr_t gameMode, uintptr_t player,
                  uintptr_t target, bool& completed)
{
    completed = false;
    if (!fnAddr || !gameMode || !player || !target)
        return 0;

    __try
    {
        const char result = reinterpret_cast<AttackFn>(fnAddr)(
            reinterpret_cast<void*>(gameMode), reinterpret_cast<void*>(player),
            reinterpret_cast<void*>(target));
        completed = true;
        return result;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

// Firma observada en la build actual (sub_1405113F0):
//   char GameMode::useItemOn(Player* player, ItemStack* item,
//                            BlockPos const& pos, unsigned char face,
//                            Vec3 const& clickAbs)
//   bool BlockItem::useOn(BlockSource& region, BlockPos const& pos,
//                         unsigned char face, Vec3 const& clickPos, Actor* by)
// ABI MSVC x64: this=rcx, player=rdx, item=r8, pos=r9, face/click en stack.
using UseItemOnFn = char(__fastcall*)(void* gameMode, void* player, void* item,
                                      const void* pos, unsigned char face,
                                      const void* click);
using BlockUseOnFn = bool(__fastcall*)(void* item, void* region, const void* pos,
                                       unsigned char face, const void* click,
                                       void* actor);

// Firma observada en la cadena de rotura (sub_140510450 / BE0):
//   char GameMode::continueDestroyBlock(Player* player, BlockPos const& pos,
//                                       unsigned char face)
// ABI MSVC x64: this=rcx, player=rdx, pos=r8, face=r9b. La funcion solo lee
// pos.x/y/z (ints) del puntero, no lo retiene despues de la llamada.
using DestroyBlockFn = char(__fastcall*)(void* gameMode, void* player,
                                         const void* pos, unsigned char face);

bool callDestroyFn(uintptr_t fnAddr, uintptr_t gameMode, uintptr_t player,
                   const BlockPos& pos, uint8_t face)
{
    if (!fnAddr || !gameMode || !player)
        return false;

    __try
    {
        return reinterpret_cast<DestroyBlockFn>(fnAddr)(
                   reinterpret_cast<void*>(gameMode), reinterpret_cast<void*>(player),
                   &pos, face) != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool callUseItemOnFn(uintptr_t fnAddr, uintptr_t gameMode, uintptr_t player,
                     uintptr_t item, const BlockPos& pos, uint8_t face,
                     const Vec3& click)
{
    if (!fnAddr || !gameMode || !player || !item)
        return false;

    auto fn = reinterpret_cast<UseItemOnFn>(fnAddr);
    bool ok = false;
    __try
    {
        ok = fn(reinterpret_cast<void*>(gameMode), reinterpret_cast<void*>(player),
                reinterpret_cast<void*>(item), &pos, face, &click);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return ok;
}

} // namespace

bool GameMode::attack(const Actor& player, const Actor& target) const
{
    if (!valid() || !player.valid() || !player.isLocal() || !target.valid() ||
        player.address() == target.address())
        return false;

    // Resolve the native entry BEFORE sending anything. The packet used to go
    // out first, so a bad mode instance left the server with an attack whose
    // local prediction never ran: the hit reached the network but the client
    // kept its own state, which is exactly the "sometimes it does nothing"
    // symptom. Now both legs require the same valid target.
    const uintptr_t table = mem::read<uintptr_t>(addr);
    constexpr int slot = vt::slot::GameMode::Attack;
    if (!table || !mem::isReadable(table + (uintptr_t)slot * sizeof(uintptr_t),
                                  sizeof(uintptr_t)))
        return false;

    const uintptr_t fn = mem::read<uintptr_t>(table + (uintptr_t)slot * sizeof(uintptr_t));
    if (!fn)
        return false;

    // Match the game's native left-click path: send VInteractPacket(action=2,
    // target runtime ID) through the live PacketSender, then apply GameMode's
    // local predicted damage.
    // On this pre-1.16 build, GameMode::attack alone only reaches the local
    // Entity::hurt path and does not create the server attack packet.
    const bool sentToServer = packets::InteractPacket::sendAttack(target);
    const float hpBefore = target.currentHealth();
    const float cacheBefore = target.cachedHealth();
    const int hurtBefore = target.hurtTime();
    bool nativeCallCompleted = false;
    const char nativeResult = callAttackFn(
        fn, addr, player.address(), target.address(), nativeCallCompleted);
    const float hpAfter = target.currentHealth();
    const float cacheAfter = target.cachedHealth();
    const int hurtAfter = target.hurtTime();
    // El id del stack en mano deja de ser opaco: si attackDamage() cae al
    // default (1.0) el log muestra que id leyo el offset de Item::mLegacyId.
    const ItemInstance held = LocalPlayer(player.address()).heldItem();
    const int heldId = held.valid() ? held.itemId() : -1;
    float heldDamage = 1.0f;
    if (held.valid())
    {
        const float candidate = held.attackDamage();
        if (std::isfinite(candidate) && candidate > 0.0f)
            heldDamage = candidate;
    }
    static uint64_t lastAttackTraceMs = 0;
    const uint64_t nowMs = GetTickCount64();
    if (nowMs - lastAttackTraceMs >= 500)
    {
        lastAttackTraceMs = nowMs;
        MC_LOG("[AttackTrace] target=%p rid=%llu sender-call=%d native-call=%d native-result=%d/0x%02X hp=%.2f->%.2f cache=%.2f->%.2f hurt=%d->%d held-id=%d dmg=%.2f",
               reinterpret_cast<void*>(target.address()),
               static_cast<unsigned long long>(target.runtimeId()),
               sentToServer ? 1 : 0, nativeCallCompleted ? 1 : 0,
               static_cast<int>(nativeResult),
               static_cast<unsigned char>(nativeResult), hpBefore, hpAfter,
               cacheBefore, cacheAfter, hurtBefore, hurtAfter, heldId,
               heldDamage);
    }
    // El server no replica la vida del objetivo, asi que el dano que acaba de
    // salir es la unica evidencia real del impacto: se anota con el arma en
    // mano para que los HUDs (TargetHUD) descuenten el valor correcto.
    if (sentToServer)
        hitfeed::recordHit(target.address(), heldDamage);

    // This only confirms that the sender call completed, not that the server
    // accepted the hit. The trace compares actor state around the native call.
    return sentToServer;
}

bool GameMode::useItem(const ItemInstance& item, uintptr_t playerAddr) const
{
    if (!valid() || !playerAddr || !item.valid())
        return false;

    const uintptr_t table = mem::read<uintptr_t>(addr);
    constexpr int slot = vt::slot::GameMode::UseItem;
    if (!table || !mem::isReadable(table + (uintptr_t)slot * sizeof(uintptr_t),
                                   sizeof(uintptr_t)))
        return false;
    const uintptr_t fn =
        mem::read<uintptr_t>(table + (uintptr_t)slot * sizeof(uintptr_t));
    if (!fn)
        return false;

    bool used = false;
    __try
    {
        using UseItemFn = char(__fastcall*)(void*, void*, void*);
        used = reinterpret_cast<UseItemFn>(fn)(
                   reinterpret_cast<void*>(addr), reinterpret_cast<void*>(playerAddr),
                   reinterpret_cast<void*>(item.address())) != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        used = false;
    }
    return used;
}

bool GameMode::useItemOn(const ItemInstance& item, const BlockPos& pos, uint8_t face,
                         const Vec3& click, uintptr_t playerAddr)
{
    if (!addr || !item.valid() || !playerAddr)
        return false;

    return callUseItemOnFn(mem::resolve(off::fn::GameModeUseItemOnWrapper),
                           addr, playerAddr, item.address(), pos, face, click);
}

bool GameMode::useItemOnVt(const ItemInstance& item, const BlockPos& pos, uint8_t face,
                           const Vec3& click, uintptr_t playerAddr)
{
    if (!item.valid() || !addr || !playerAddr ||
        !mem::isReadable(addr, sizeof(uintptr_t)))
        return false;

    // Dispatch through the live mode's vtable. The current build's
    // SurvivalMode slot 12 is a guard wrapper distinct from the base method.
    uintptr_t vt = mem::read<uintptr_t>(addr);
    int slot = vt::slot::GameMode::UseItemOn;
    if (!vt || slot < 0 || !mem::isReadable(vt + (uintptr_t)slot * 8, 8))
        return false;

    uintptr_t fn = mem::read<uintptr_t>(vt + (uintptr_t)slot * 8);
    return callUseItemOnFn(fn, addr, playerAddr, item.address(), pos, face, click);
}

bool GameMode::continueDestroyBlock(uintptr_t playerAddr, const BlockPos& pos,
                                   uint8_t face) const
{
    if (!addr || !playerAddr || !mem::isReadable(addr, sizeof(uintptr_t)))
        return false;

    const uintptr_t table = mem::read<uintptr_t>(addr);
    constexpr int slot = vt::slot::GameMode::ContinueDestroy;
    if (!table || !mem::isReadable(table + (uintptr_t)slot * sizeof(uintptr_t),
                                   sizeof(uintptr_t)))
        return false;

    const uintptr_t fn = mem::read<uintptr_t>(table + (uintptr_t)slot * sizeof(uintptr_t));
    return callDestroyFn(fn, addr, playerAddr, pos, face);
}

bool GameMode::startDestroyBlock(uintptr_t playerAddr, const BlockPos& pos,
                                 uint8_t face) const
{
    if (!addr || !playerAddr || !mem::isReadable(addr, sizeof(uintptr_t)))
        return false;

    const uintptr_t table = mem::read<uintptr_t>(addr);
    constexpr int slot = vt::slot::GameMode::StartDestroy;
    if (!table || !mem::isReadable(table + (uintptr_t)slot * sizeof(uintptr_t),
                                   sizeof(uintptr_t)))
        return false;

    const uintptr_t fn = mem::read<uintptr_t>(table + (uintptr_t)slot * sizeof(uintptr_t));
    return callDestroyFn(fn, addr, playerAddr, pos, face);
}

bool GameMode::stopDestroyBlock(uintptr_t playerAddr) const
{
    if (!addr || !playerAddr || !mem::isReadable(addr, sizeof(uintptr_t)))
        return false;

    const uintptr_t table = mem::read<uintptr_t>(addr);
    constexpr int slot = vt::slot::GameMode::StopDestroy;
    if (!table || !mem::isReadable(table + (uintptr_t)slot * sizeof(uintptr_t),
                                   sizeof(uintptr_t)))
        return false;

    const uintptr_t fn = mem::read<uintptr_t>(table + (uintptr_t)slot * sizeof(uintptr_t));
    if (!fn)
        return false;

    // REA sub_140510FE0: (GameMode*, Player*) — sin BlockPos.
    bool completed = false;
    __try
    {
        reinterpret_cast<char(__fastcall*)(void*, void*)>(fn)(
            reinterpret_cast<void*>(addr), reinterpret_cast<void*>(playerAddr));
        completed = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return completed;
}

float GameMode::destroyProgress() const
{
    if (!addr || !mem::isReadable(addr + off::gamemode::DestroyProgress, sizeof(float)))
        return 0.0f;
    return mem::read<float>(addr + off::gamemode::DestroyProgress);
}

bool GameMode::destroyTarget(BlockPos& out) const
{
    if (!addr ||
        !mem::isReadable(addr + off::gamemode::DestroyPosX, sizeof(int) * 3))
        return false;

    out.x = mem::read<int>(addr + off::gamemode::DestroyPosX);
    out.y = mem::read<int>(addr + off::gamemode::DestroyPosY);
    out.z = mem::read<int>(addr + off::gamemode::DestroyPosZ);
    return true;
}

void GameMode::setDestroyProgress(float value) const
{
    if (!addr || !mem::isReadable(addr + off::gamemode::DestroyProgress, sizeof(float)))
        return;
    mem::write<float>(addr + off::gamemode::DestroyProgress, value);
}

void GameMode::clearDestroyCooldown() const
{
    if (!addr ||
        !mem::isReadable(addr + off::gamemode::NextDestroyTime, sizeof(uint64_t)))
        return;
    mem::write<uint64_t>(addr + off::gamemode::NextDestroyTime, 0);
}

bool blockItemUseOn(const ItemInstance& item, const BlockPos& pos, uint8_t face,
                    const Vec3& click, uintptr_t playerAddr)
{
    if (!item.valid() || !playerAddr)
        return false;

    uintptr_t itemPtr = item.item();
    if (!itemPtr)
        return false;

    BlockSource bs = Game::get().blockSource();
    if (!bs.valid())
        return false;

    auto fn = reinterpret_cast<BlockUseOnFn>(mem::resolve(off::fn::BlockItemUseOn));
    if (!fn)
        return false;

    bool ok = false;
    __try
    {
        ok = fn(reinterpret_cast<void*>(itemPtr), reinterpret_cast<void*>(bs.address()),
                &pos, face, &click, reinterpret_cast<void*>(playerAddr));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return ok;
}

// ============================================================================
// Puerto de Solstice BlockUtils::placeBlock.
//
// LA entrada real de colocacion del cliente Bedrock es
//   GameMode::buildBlock(BlockPos const& baseCell, uchar blockSide, bool)
// (NO useItemOn): lee el HitResult del crosshair, coloca localmente y
// arma+envia el UseItemPacket. La forja del hitresult hace que el juego
// "mire" exactamente el bloque base + cara que queremos — y buildBlock
// hace el resto, igual que un right-click real. Arquitectura identica a
// Solstice placeBlock (BlockUtils.cpp:261).
//
// Off[face] (tabla Solstice blockFaceOffsets): airCell -> baseCell con
// face = la cara estandar del bloque base por la que se coloca.
// ============================================================================
void placeFaceOffset(int face, int& dx, int& dy, int& dz)
{
    static constexpr int kOff[6][3] = {
        {0, 1, 0},   // 0 bottom: base ARRIBA de la celda aire
        {0, -1, 0},  // 1 top: base abajo
        {0, 0, 1},   // 2 north: base a +z
        {0, 0, -1},  // 3 south: base a -z
        {1, 0, 0},   // 4 west: base a +x
        {-1, 0, 0},  // 5 east: base a -x
    };
    if (face < 0 || face > 5)
    {
        dx = dy = dz = 0;
        return;
    }
    dx = kOff[face][0];
    dy = kOff[face][1];
    dz = kOff[face][2];
}

bool forgePlaceBlock(const BlockPos& airCell, uint8_t face)
{
    if (face > 5)
        return false;

    uintptr_t fn = mem::resolve(off::fn::GameModeBuildBlock);
    if (!fn)
        return false; // RVA pendiente del IDA (xrefs a 0x5100F0/0x510270)

    Game& game = Game::get();
    GameMode gm = game.gameMode();
    if (!gm.valid())
        return false;

    uintptr_t level = game.levelAddr();
    if (!level)
        return false;

    // HitResult del crosshair (Level + off::chain::LevelHitResult).
    uintptr_t hit = level + off::chain::LevelHitResult;
    if (off::chain::LevelHitResultIsPointer)
    {
        hit = mem::read<uintptr_t>(hit);
        if (!hit)
            return false;
    }
    if (!mem::isReadable(hit, 0x70))
        return false;

    int ox = 0, oy = 0, oz = 0;
    placeFaceOffset(face, ox, oy, oz);
    BlockPos base{airCell.x + ox, airCell.y + oy, airCell.z + oz};

    // MSVC: funciones con __try separadas del resto (no mezclar con
    // objetos que requieren unwinding).
    bool forged = false;
    __try
    {
        // Forja del HitResult (layout Solstice 1.21, clase estable):
        //   +0x0C vec3 mRayDir, +0x18 int mType, +0x1C uchar mFacing,
        //   +0x20 ivec3 mBlockPos, +0x2C vec3 mPos, +0x6C bool mIndirectHit.
        mem::write<float>(hit + 0x18, 0.0f);           // mType = BLOCK(0)
        mem::write<uint8_t>(hit + 0x1C, face);        // mFacing
        mem::write<int>(hit + 0x20, base.x);          // mBlockPos = baseCell
        mem::write<int>(hit + 0x24, base.y);
        mem::write<int>(hit + 0x28, base.z);
        mem::write<float>(hit + 0x2C, (float)airCell.x); // mPos = airCell
        mem::write<float>(hit + 0x30, (float)airCell.y);
        mem::write<float>(hit + 0x34, (float)airCell.z);
        mem::write<float>(hit + 0x0C, (float)base.x);     // mRayDir = baseCell
        mem::write<float>(hit + 0x10, (float)base.y);
        mem::write<float>(hit + 0x14, (float)base.z);
        mem::write<uint8_t>(hit + 0x6C, 0);           // mIndirectHit = false
        forged = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    if (!forged)
        return false;

    // buildBlock(this, &baseCell, face, true) — firma Solstice:
    //   virtual void buildBlock(glm::ivec3 const&, uchar blockSide, bool)
    // ivec3 (12 bytes) por REFERENCIA en MSVC x64.
    using BuildBlockFn = void(__fastcall*)(void*, const int*, unsigned char, bool);
    auto fnB = reinterpret_cast<BuildBlockFn>(fn);
    int pos3[3] = {base.x, base.y, base.z};
    __try
    {
        fnB(reinterpret_cast<void*>(gm.address()), pos3, face, true);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return true;
}

// ============================================================================
// LA colocacion — GameMode::useItemOn (sub_1405113F0, vtable slot 12),
// firma confirmada por decompile:
//   char useItemOn(this, Player* player, ItemStack* item, BlockPos const& pos,
//                  unsigned char face, Vec3 const& clickAbs)
// Hace TODO lo que un right-click real del juego:
//   1. Si el juego esta en modo red, arma el UseItemPacket (pos, face,
//      click RELATIVO, playerPos, flag=hotbar slot) y lo envia por el
//      PacketSender interno (this+8, vtable slot 2).
//   2. Interactua con el bloque clickeado (usar puertas etc.).
//   3. Place local via sub_14055D960(ItemStack*, Player*, x,y,z, face,
//      clickRel) — y consume el item del slot.
// El clickAbs va en coordenadas de mundo (la funcion lo relativiza).
// ============================================================================
bool placeBlockFull(const ItemInstance& held, const BlockPos& base,
                    uint8_t face, const Vec3& clickAbs, uintptr_t playerAddr)
{
    if (!held.valid() || !playerAddr)
        return false;

    GameMode gm = Game::get().gameMode();
    if (!gm.valid())
        return false;

    // Dispatch through the live game-mode override. Calling the base RVA with
    // a guessed ABI faulted in the server session (READ @ -1); the virtual
    // call is safe and returns whether the native path accepted the action.
    return gm.useItemOnVt(held, base, face, clickAbs, playerAddr);
}

// ============================================================================
// Plan B: UseItemPacket manual — backup si el orquestador cambia de firma.
// Construye el paquete con el ctor REAL (sub_1405100F0) y lo envia por
// sendToServer con los handles capturados del hook del Chat.
//   ctor(this, &blockPos, face, itemStack, &playerPos, &clickRel, flag=slot)
//   (param 5 -> packet+0x20 = playerPos; param 6 -> +0x2C = click relativo;
//    flag = hotbar slot seleccionado, como hace el caller real)
// ============================================================================
bool sendPlacePacket(const ItemInstance& held, const BlockPos& base,
                     uint8_t face, const Vec3& clickAbs, const Vec3& playerPos,
                     int flag)
{
    if (!held.valid())
        return false;

    auto ctor = reinterpret_cast<void*(__fastcall*)(
        void*, const int*, char, void*, const float*, const float*, int)>(
        mem::resolve(packets::UseItemPacket::CtorRva));
    if (!ctor)
        return false;

    // Click relativo al bloque (lo hace el orquestador real antes del ctor).
    Vec3 rel{clickAbs.x - (float)base.x, clickAbs.y - (float)base.y,
            clickAbs.z - (float)base.z};

    // Buffer del paquete: header + item copiado en +0x38 (~0x60 bytes).
    __declspec(align(16)) unsigned char pkt[160] = {};

    bool built = false;
    __try
    {
        ctor(pkt, &base.x, (char)face, reinterpret_cast<void*>(held.address()),
             &playerPos.x, &rel.x, flag);
        built = mem::read<uintptr_t>((uintptr_t)pkt) ==
                mem::resolve(packets::UseItemPacket::VTableRva);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        built = false;
    }
    if (!built)
        return false;

    // Gameplay packets travel through PacketSender::send. sendToServer is a
    // different path and does not carry the native UseItemPacket placement.
    const bool sent = packets::PacketSender::sendGameplay(pkt);
    destroyItemStackCopy(reinterpret_cast<uintptr_t>(pkt) +
                         packets::UseItemPacket::ItemOffset);
    return sent;
}

bool applyLocalBlockPlacement(const ItemInstance& held, uintptr_t playerAddr,
                              const BlockPos& base, uint8_t face,
                              const Vec3& clickAbs)
{
    if (!held.valid() || !playerAddr || face > 5)
        return false;

    const uintptr_t fn = mem::resolve(off::fn::ItemStackUseOn);
    if (!fn)
        return false;

    const Vec3 clickRel{clickAbs.x - static_cast<float>(base.x),
                        clickAbs.y - static_cast<float>(base.y),
                        clickAbs.z - static_cast<float>(base.z)};
    bool placed = false;
    __try
    {
        // Native GameMode::useItemOn calls sub_14055D960 with:
        // (ItemStack*, Player*, x, y, z, face, clickRel.x, clickRel.y, clickRel.z).
        using ItemStackUseOnFn = uintptr_t(__fastcall*)(
            void*, void*, uint32_t, int, int, char, float, float, float);
        placed = reinterpret_cast<ItemStackUseOnFn>(fn)(
                     reinterpret_cast<void*>(held.address()),
                     reinterpret_cast<void*>(playerAddr),
                     static_cast<uint32_t>(base.x), base.y, base.z,
                     static_cast<char>(face), clickRel.x, clickRel.y, clickRel.z) != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    if (placed && mem::read<uint8_t>(held.address()) == 0)
    {
        // Match GameMode::useItemOn's cleanup when the used stack becomes
        // empty: Inventory::removeItem(selectedSlot), vtable slot 29.
        const uintptr_t inventory =
            mem::read<uintptr_t>(playerAddr + off::player::InventoryPtr);
        if (inventory && mem::isReadable(inventory, sizeof(uintptr_t)))
        {
            const uintptr_t inventoryVtable = mem::read<uintptr_t>(inventory);
            constexpr uintptr_t removeSlot = 29;
            const uintptr_t entry = inventoryVtable + removeSlot * sizeof(uintptr_t);
            if (inventoryVtable && mem::isReadable(entry, sizeof(uintptr_t)))
            {
                const uintptr_t removeFn = mem::read<uintptr_t>(entry);
                const int selectedSlot = mem::read<int>(inventory + off::inv::SelectedSlot);
                if (removeFn && selectedSlot >= 0 && selectedSlot <= 8)
                {
                    __try
                    {
                        using RemoveItemFn = void(__fastcall*)(void*, int);
                        reinterpret_cast<RemoveItemFn>(removeFn)(
                            reinterpret_cast<void*>(inventory), selectedSlot);
                    }
                    __except (EXCEPTION_EXECUTE_HANDLER)
                    {
                    }
                }
            }
        }
    }

    return placed;
}

// ============================================================================
// Tracer: identificacion empirica del slot de GameMode::useItemOn.
// Hookea los 5 candidatos de la vtable GameMode (dump de IDA):
//   slot 9  = sub_140511BC0    slot 10 = sub_140511820
//   slot 11 = sub_140511700    slot 12 = sub_1405113F0 (wrapper actual)
//   slot 15 = sub_1405118A0
// Con el tracer armado, un right-click NATURAL colocando un bloque revela
// en el log que slot disparo y con que argumentos — firma real resuelta
// sin adivinar. Relay generico de 6 slots (this + 5 params: las firmas
// candidatas son de esa aridad), SEH en el passthrough.
// ============================================================================

namespace {

struct GmTraceHook
{
    void* target = nullptr;
    void* original = nullptr;
    bool installed = false;
};

struct GmCandidate
{
    int slot;
    uintptr_t rva;
};

constexpr GmCandidate kGmCandidates[5] = {
    {9, 0x511BC0},
    {10, 0x511820},
    {11, 0x511700},
    {12, 0x5113F0},
    {15, 0x5118A0},
};

GmTraceHook g_gmHooks[5];
ULONGLONG g_traceUntil = 0;
// Cap POR SLOT (3): los slots 9/10 son virtuales per-frame que queman
// cualquier presupuesto global antes del right-click. Con cap individual
// la ventana de 30s sobrevive y el evento raro (place) queda loggeado.
unsigned g_traceSlotLogs[5] = {};

void logTraceCall(int idx, void* a1, void* a2, void* a3, void* a4, void* a5, void* a6)
{
    ULONGLONG now = GetTickCount64();
    if (now > g_traceUntil || g_traceSlotLogs[idx] >= 3)
        return;
    ++g_traceSlotLogs[idx];

    // Marca si rdx parece el BlockSource (vtable conocida) — ayuda a
    // confirmar la firma (this, BlockSource&, ...) sin salir del log.
    const char* bsMark = "";
    if (a2 && mem::isReadable((uintptr_t)a2, sizeof(uintptr_t)) &&
        mem::read<uintptr_t>((uintptr_t)a2) == mem::resolve(vt::BlockSource))
        bsMark = "  <-- rdx=BlockSource!";

    MC_LOG("[GMTrace] slot %d (sub_%06llX): rcx=%p rdx=%p r8=%p r9=%p "
           "[20]=%p [28]=%p%s",
           kGmCandidates[idx].slot, (unsigned long long)kGmCandidates[idx].rva,
           a1, a2, a3, a4, a5, a6, bsMark);
}

#define GM_DETOUR(N)                                                                \
    long long __fastcall gmDetour##N(void* a1, void* a2, void* a3, void* a4,         \
                                     void* a5, void* a6)                             \
    {                                                                                \
        logTraceCall(N, a1, a2, a3, a4, a5, a6);                                      \
        auto o = reinterpret_cast<long long(__fastcall*)(                            \
            void*, void*, void*, void*, void*, void*)>(g_gmHooks[N].original);        \
        long long r = 0;                                                             \
        __try                                                                        \
        {                                                                            \
            r = o(a1, a2, a3, a4, a5, a6);                                            \
        }                                                                            \
        __except (EXCEPTION_EXECUTE_HANDLER)                                         \
        {                                                                            \
        }                                                                            \
        return r;                                                                    \
    }

GM_DETOUR(0)
GM_DETOUR(1)
GM_DETOUR(2)
GM_DETOUR(3)
GM_DETOUR(4)

void* const kGmDetours[5] = {
    (void*)&gmDetour0, (void*)&gmDetour1, (void*)&gmDetour2,
    (void*)&gmDetour3, (void*)&gmDetour4,
};

} // namespace

void traceUseItemOn(unsigned seconds)
{
    if (!hook::initialize())
        return;

    for (int i = 0; i < 5; ++i)
    {
        if (g_gmHooks[i].installed)
            continue;
        void* target = reinterpret_cast<void*>(mem::resolve(kGmCandidates[i].rva));
        if (!target)
            continue;
        if (hook::create(target, kGmDetours[i], &g_gmHooks[i].original))
        {
            g_gmHooks[i].target = target;
            g_gmHooks[i].installed = true;
            MC_LOG("[GMTrace] hook slot %d (sub_%06llX) instalado",
                   kGmCandidates[i].slot, (unsigned long long)kGmCandidates[i].rva);
        }
    }

    g_traceUntil = GetTickCount64() + (ULONGLONG)seconds * 1000ull;
    for (int i = 0; i < 5; ++i)
        g_traceSlotLogs[i] = 0;
    MC_LOG("[GMTrace] ARMADO %us — coloca un bloque con RIGHT-CLICK natural "
           "y revisa el log (slots 9/10/11/12/15, max 3 logs por slot)",
           seconds);
}

}
