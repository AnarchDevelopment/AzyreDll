#pragma once

#include "Framework/Memory.hpp"
#include "Framework/Math.hpp"
#include "SDK/Engine/Memory/Offsets.hpp"
#include "SDK/Engine/VTable/VTables.hpp"
#include "SDK/Inventory/ItemInstance/ItemInstance.hpp"

namespace mc::off {

// Estado interno de la rotura de bloques (GameMode). Confirmado por disasm del
// build actual: continueDestroyBlock (slot 3, sub_140510BE0) compara la pos en
// curso con la pedida, acumula DestroyProgress += dt * destroySpeed * 20 y
// dispara destroyBlock (slot 2) cuando el progreso alcanza 1.0.
namespace gamemode
{
    constexpr uintptr_t DestroyPosX = 0x28;      // BlockPos en curso (3 ints)
    constexpr uintptr_t DestroyPosY = 0x2C;
    constexpr uintptr_t DestroyPosZ = 0x30;
    constexpr uintptr_t DestroyProgress = 0x38;  // float acumulado 0..1
    constexpr uintptr_t LastUpdateTime = 0x40;   // double, segundos (QPC)
    constexpr uintptr_t NextDestroyTime = 0x58;  // uint64 ns; si > now, el tick se ignora
    constexpr uintptr_t NextParticleTime = 0x60; // uint64 ns del proximo efecto
    constexpr uintptr_t DestroyDelayMs = 0x68;   // int ms entre roturas
    constexpr uintptr_t ParticleIntervalMs = 0x78;
}

}

namespace mc {

class Actor;

class GameMode
{
public:
    GameMode() = default;
    explicit GameMode(uintptr_t a) : addr(a) {}

    bool valid() const
    {
        if (!addr || !mem::isReadable(addr, sizeof(uintptr_t)))
            return false;
        uintptr_t v = mem::read<uintptr_t>(addr);
        return v == mem::resolve(vt::CreativeMode) ||
               v == mem::resolve(vt::SurvivalMode) ||
               v == mem::resolve(vt::GameMode);
    }
    bool isSurvival() const
    {
        return valid() && mem::read<uintptr_t>(addr) == mem::resolve(vt::SurvivalMode);
    }
    uintptr_t address() const { return addr; }

    // Despacha el ataque por la vtable activa del modo (Survival/Creative/base).
    bool attack(const Actor& player, const Actor& target) const;

    // Native GameMode::useItem (vtable slot 11): invokes the item's local use
    // behavior and sends the matching UseItemPacket through the game's sender.
    bool useItem(const ItemInstance& item, uintptr_t playerAddr) const;

    // continueDestroyBlock (vtable slot 3 = sub_140510BE0): acumula el progreso
    // de rotura del bloque en curso y aplica la rotura local + paquete cuando
    // llega a 1.0. `pos` debe ser la misma BlockPos que el GameMode tiene en
    // curso (destroyTarget); si no coincide, la funcion reinicia el ciclo y
    // reenvia el PlayerActionPacket de inicio.
    bool continueDestroyBlock(uintptr_t playerAddr, const BlockPos& pos,
                              uint8_t face) const;

    // startDestroyBlock (vtable slot 1 = sub_140510450): abre el ciclo de
    // rotura sobre `pos`. Con DestroyProgress == 0 arma y envia el
    // PlayerActionPacket de inicio por el PacketSender interno (this+8) y
    // fija NextDestroyTime. Es la mitad que el click izquierdo real ejecuta al
    // empezar a minar; continueDestroyBlock hace el resto del ciclo.
    bool startDestroyBlock(uintptr_t playerAddr, const BlockPos& pos,
                           uint8_t face) const;

    // stopDestroyBlock (vtable slot 4 = sub_140510FE0): firma (GameMode*,
    // Player*) confirmada por decompile, sin BlockPos. Arma el
    // PlayerActionPacket de aborto cuando el modo esta en red y limpia
    // DestroyProgress (+0x38) y NextDestroyTime (+0x58). Es lo que el click
    // real ejecuta al soltar el boton.
    bool stopDestroyBlock(uintptr_t playerAddr) const;

    // Estado de la rotura en curso, leido sin llamar a nada del juego.
    float destroyProgress() const;
    bool destroyTarget(BlockPos& out) const;
    void setDestroyProgress(float value) const;
    // Pone a 0 el timestamp del proximo tick permitido (<0x58>), saltandose el
    // delay entre roturas que el juego usa en creativo y tras cada bloque.
    void clearDestroyCooldown() const;

    bool useItemOn(const ItemInstance& item, const BlockPos& pos, uint8_t face,
                   const Vec3& click, uintptr_t playerAddr);
    bool useItemOnVt(const ItemInstance& item, const BlockPos& pos, uint8_t face,
                     const Vec3& click, uintptr_t playerAddr);

private:
    uintptr_t addr = 0;
};

bool blockItemUseOn(const ItemInstance& item, const BlockPos& pos, uint8_t face,
                    const Vec3& click, uintptr_t playerAddr);

// LA colocacion — GameMode::useItemOn (sub_1405113F0, slot 12) con la
// firma confirmada por decompile: (this, Player*, ItemStack*, BlockPos&,
// face, clickAbs). Hace TODO un right-click real: paquete al server,
// interact, place local y consume del item. Local + servidores.
bool placeBlockFull(const ItemInstance& held, const BlockPos& base,
                    uint8_t face, const Vec3& clickAbs, uintptr_t playerAddr);

// Puerto de Solstice BlockUtils::placeBlock: la colocacion REAL de Bedrock.
// Forja el HitResult del crosshair hacia (baseCell, face) y llama
// GameMode::buildBlock — que aplica local y arma+envia el UseItemPacket.
// airCell = celda AIRE a ocupar; el bloque base se deriva con la tabla
// placeFaceOffset. Requiere off::fn::GameModeBuildBlock + off::chain::
// LevelHitResult (pendientes del IDA: xrefs a los ctors de UseItemPacket).
bool forgePlaceBlock(const BlockPos& airCell, uint8_t face);

// Fallback para sesiones donde el guard de SurvivalMode rechaza useItemOn:
// UseItemPacket construido con el ctor REAL del juego (sub_1405100F0) y
// enviado por PacketSender::send (ruta de paquetes de gameplay). clickAbs va
// en absolutos; se convierte a coordenadas relativas del bloque.
bool sendPlacePacket(const ItemInstance& held, const BlockPos& base,
                     uint8_t face, const Vec3& clickAbs, const Vec3& playerPos,
                     int flag);

// Apply the local half of the native GameMode::useItemOn sequence without
// sending a second packet. Used after Scaffold's server packet so the client
// predicts the block and placement hooks observe it immediately.
bool applyLocalBlockPlacement(const ItemInstance& held, uintptr_t playerAddr,
                              const BlockPos& base, uint8_t face,
                              const Vec3& clickAbs);

// off[face] = airCell -> baseCell (tabla Solstice, cara estandar Bedrock).
void placeFaceOffset(int face, int& dx, int& dy, int& dz);

// Diagnostico: hookea los 5 slots candidatos de la vtable GameMode y arma
// una ventana de `seconds` en la que TODO call queda loggeado con sus
// argumentos. Un right-click natural colocando un bloque identifica el
// slot real de useItemOn y su firma (ver [GMTrace] en el log).
void traceUseItemOn(unsigned seconds);

}
