#pragma once

#include "Framework/Memory.hpp"
#include "Framework/Math.hpp"
#include "SDK/Offsets.hpp"
#include "SDK/VTables.hpp"
#include "SDK/Classes/ItemInstance.hpp"

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
    uintptr_t address() const { return addr; }

    // Despacha el ataque por la vtable activa del modo (Survival/Creative/base).
    bool attack(const Actor& player, const Actor& target) const;

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

// Plan B (activo ya): UseItemPacket construido con el ctor REAL del juego
// (sub_1405100F0) y enviado por sendToServer con los handles capturados
// del hook del Chat. clickAbs va en absolutos (se relativiza adentro).
bool sendPlacePacket(const ItemInstance& held, const BlockPos& base,
                     uint8_t face, const Vec3& clickAbs, const Vec3& playerPos,
                     int flag);

// off[face] = airCell -> baseCell (tabla Solstice, cara estandar Bedrock).
void placeFaceOffset(int face, int& dx, int& dy, int& dz);

// Diagnostico: hookea los 5 slots candidatos de la vtable GameMode y arma
// una ventana de `seconds` en la que TODO call queda loggeado con sus
// argumentos. Un right-click natural colocando un bloque identifica el
// slot real de useItemOn y su firma (ver [GMTrace] en el log).
void traceUseItemOn(unsigned seconds);

}
