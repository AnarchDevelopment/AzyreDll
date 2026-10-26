#include "GameMode.hpp"

#include "Framework/Hook.hpp"
#include "Framework/Log.hpp"
#include "SDK/ChatHook.hpp"
#include "SDK/Game.hpp"
#include "SDK/VTables.hpp"
#include "SDK/Classes/Actor.hpp"

#include <excpt.h>

namespace mc {

namespace {

using AttackFn = bool(__fastcall*)(void* gameMode, void* player, void* target);

struct InteractAttackPacket
{
    uintptr_t vtable;
    uint32_t type;
    uint32_t field;
    uint8_t action;
    uint8_t padding[7];
    uint64_t targetRuntimeId;
};
static_assert(sizeof(InteractAttackPacket) == 0x20);
static_assert(offsetof(InteractAttackPacket, action) == off::interactPacket::Action);
static_assert(offsetof(InteractAttackPacket, targetRuntimeId) ==
              off::interactPacket::TargetRuntimeID);

bool sendInteractAttackPacket(const Actor& target)
{
    if (!target.valid() || !mem::isReadable(target.address() + off::player::RuntimeID,
                                             sizeof(uint64_t)))
        return false;

    const uint64_t runtimeId =
        mem::read<uint64_t>(target.address() + off::player::RuntimeID);
    if (runtimeId == 0 || runtimeId == UINT64_MAX)
        return false;

    // The game's native left-click handler (sub_14004B9D0) sends this packet
    // through MinecraftClient + 0xE8 -> connector + 0x98 -> PacketSender vslot 2.
    // sendToServer is a different path and bypasses the gameplay sender used
    // for entity interactions, so use the live sender instance directly.
    const uintptr_t client = Game::get().clientAddr();
    if (!client || !mem::isReadable(client, off::chain::ClientNetworkConnector + sizeof(uintptr_t)))
        return false;

    const uintptr_t connector =
        mem::read<uintptr_t>(client + off::chain::ClientNetworkConnector);
    if (!connector || !mem::isReadable(
                          connector,
                          off::chain::NetworkConnectorPacketSender + sizeof(uintptr_t)))
        return false;

    const uintptr_t sender =
        mem::read<uintptr_t>(connector + off::chain::NetworkConnectorPacketSender);
    if (!sender || !mem::isReadable(sender, sizeof(uintptr_t)))
        return false;

    const uintptr_t senderVtable = mem::read<uintptr_t>(sender);
    const uintptr_t packetSenderVtable = mem::resolve(vt::PacketSender);
    const uintptr_t batchedSenderVtable = mem::resolve(vt::BatchedPacketSender);
    if (senderVtable != packetSenderVtable && senderVtable != batchedSenderVtable)
        return false;

    constexpr int sendSlot = vt::slot::PacketSender::Send;
    if (!mem::isReadable(senderVtable + (uintptr_t)sendSlot * sizeof(uintptr_t),
                         sizeof(uintptr_t)))
        return false;
    const uintptr_t sendFn =
        mem::read<uintptr_t>(senderVtable + (uintptr_t)sendSlot * sizeof(uintptr_t));
    if (!sendFn)
        return false;

    InteractAttackPacket packet{};
    packet.vtable = mem::resolve(vt::InteractPacket);
    packet.type = 2;
    packet.field = 1;
    packet.action = off::interactPacket::AttackAction;
    packet.targetRuntimeId = runtimeId;
    if (!packet.vtable)
        return false;

    bool sent = false;
    __try
    {
        using PacketSenderSendFn = void(__fastcall*)(void*, void*);
        reinterpret_cast<PacketSenderSendFn>(sendFn)(
            reinterpret_cast<void*>(sender), &packet);
        sent = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sent = false;
    }
    return sent;
}

bool callAttackFn(uintptr_t fnAddr, uintptr_t gameMode, uintptr_t player,
                  uintptr_t target)
{
    if (!fnAddr || !gameMode || !player || !target)
        return false;

    bool attacked = false;
    __try
    {
        attacked = reinterpret_cast<AttackFn>(fnAddr)(
            reinterpret_cast<void*>(gameMode), reinterpret_cast<void*>(player),
            reinterpret_cast<void*>(target));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return attacked;
}

// Firmas REALES de Bedrock (headers publicos de la era de este build):
//   bool GameMode::useItemOn(BlockSource& region, ItemStack const& item,
//                            BlockPos const& pos, unsigned char face,
//                            Vec3 const& click)
//   bool BlockItem::useOn(BlockSource& region, BlockPos const& pos,
//                         unsigned char face, Vec3 const& clickPos, Actor* by)
// ABI MSVC x64: this=rcx, region=rdx, item/pos=r8, pos/face=r9, y los
// structs de 12 bytes (BlockPos/Vec3) viajan por REFERENCIA en el stack.
// (Las firmas anteriores del SDK pasaban ItemInstance/Player/x/y/z — ABI
// inventada: jamas pudo funcionar.)
using UseItemOnFn = bool(__fastcall*)(void* gameMode, void* region, void* item,
                                      const void* pos, unsigned char face,
                                      const void* click);
using BlockUseOnFn = bool(__fastcall*)(void* item, void* region, const void* pos,
                                       unsigned char face, const void* click,
                                       void* actor);

bool callUseItemOnFn(uintptr_t fnAddr, uintptr_t gameMode, uintptr_t region,
                     uintptr_t item, const BlockPos& pos, uint8_t face,
                     const Vec3& click)
{
    if (!fnAddr || !gameMode || !region || !item)
        return false;

    auto fn = reinterpret_cast<UseItemOnFn>(fnAddr);
    bool ok = false;
    __try
    {
        ok = fn(reinterpret_cast<void*>(gameMode), reinterpret_cast<void*>(region),
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

    // Match the game's native left-click path: send VInteractPacket(action=2,
    // target runtime ID) through the live PacketSender, then apply GameMode's
    // local predicted damage.
    // On this pre-1.16 build, GameMode::attack alone only reaches the local
    // Entity::hurt path and does not create the server attack packet.
    const bool sentToServer = sendInteractAttackPacket(target);

    const uintptr_t table = mem::read<uintptr_t>(addr);
    constexpr int slot = vt::slot::GameMode::Attack;
    if (!table || !mem::isReadable(table + (uintptr_t)slot * sizeof(uintptr_t),
                                   sizeof(uintptr_t)))
        return false;

    const uintptr_t fn = mem::read<uintptr_t>(table + (uintptr_t)slot * sizeof(uintptr_t));
    const bool predictedLocally = callAttackFn(fn, addr, player.address(),
                                               target.address());
    // Local prediction alone is not a server hit. Report success only if the
    // gameplay sender accepted the packet call.
    (void)predictedLocally;
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
                         const Vec3& click, uintptr_t /*playerAddr*/)
{
    if (!addr || !item.valid())
        return false;

    BlockSource bs = Game::get().blockSource();
    if (!bs.valid())
        return false;

    return callUseItemOnFn(mem::resolve(off::fn::GameModeUseItemOnWrapper),
                           addr, bs.address(), item.address(), pos, face, click);
}

bool GameMode::useItemOnVt(const ItemInstance& item, const BlockPos& pos, uint8_t face,
                           const Vec3& click, uintptr_t /*playerAddr*/)
{
    if (!item.valid() || !addr || !mem::isReadable(addr, sizeof(uintptr_t)))
        return false;

    BlockSource bs = Game::get().blockSource();
    if (!bs.valid())
        return false;

    // Dispatch REAL: la vtable DE LA INSTANCIA. useItemOn es virtual y
    // SurvivalMode/CreativeMode pueden override — la vtable estatica de la
    // clase base no aplica para el objeto vivo.
    uintptr_t vt = mem::read<uintptr_t>(addr);
    int slot = vt::slot::GameMode::UseItemOn;
    if (!vt || slot < 0 || !mem::isReadable(vt + (uintptr_t)slot * 8, 8))
        return false;

    uintptr_t fn = mem::read<uintptr_t>(vt + (uintptr_t)slot * 8);
    return callUseItemOnFn(fn, addr, bs.address(), item.address(), pos, face, click);
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

    auto fn = reinterpret_cast<char(__fastcall*)(
        void*, void*, void*, const int*, unsigned char, const float*)>(
        mem::resolve(off::fn::GameModeUseItemOnWrapper));
    if (!fn)
        return false;

    bool ok = false;
    __try
    {
        ok = fn(reinterpret_cast<void*>(gm.address()),
                reinterpret_cast<void*>(playerAddr),
                reinterpret_cast<void*>(held.address()),
                &base.x, face, &clickAbs.x) != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }
    return ok;
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
        mem::resolve(off::fn::UseItemPacketCtor));
    if (!ctor)
        return false;

    // Click relativo al bloque (lo hace el orquestador real antes del ctor).
    Vec3 rel{clickAbs.x - (float)base.x, clickAbs.y - (float)base.y,
            clickAbs.z - (float)base.z};

    // Buffer del paquete: header + item copiado en +0x38 (~0x60 bytes).
    __declspec(align(16)) static unsigned char pkt[160] = {};

    bool built = false;
    __try
    {
        ctor(pkt, &base.x, (char)face, reinterpret_cast<void*>(held.address()),
             &playerPos.x, &rel.x, flag);
        built = mem::read<uintptr_t>((uintptr_t)pkt) ==
                mem::resolve(vt::UseItemPacket);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        built = false;
    }
    if (!built)
        return false;

    return chathook::sendRawPacket(pkt);
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
