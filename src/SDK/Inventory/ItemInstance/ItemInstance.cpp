#include "ItemInstance.hpp"

#include <excpt.h>

namespace mc {

int ItemInstance::itemId() const
{
    uintptr_t ip = item();
    if (!ip || !mem::isReadable(ip + off::item::LegacyId, sizeof(int16_t)))
        return -1;

    int result = -1;
    __try
    {
        result = static_cast<int>(mem::read<int16_t>(ip + off::item::LegacyId));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        result = -1;
    }
    return result;
}

float ItemInstance::attackDamage() const
{
    // Tabla de dano base de PE 0.15 (compatible con la de Java 1.8): el server
    // aplica este valor al impactar, sin contar encantamientos ni armadura.
    switch (itemId())
    {
    case 267: return 6.0f; // iron_sword
    case 268: return 4.0f; // wooden_sword
    case 272: return 5.0f; // stone_sword
    case 276: return 7.0f; // diamond_sword
    case 283: return 4.0f; // golden_sword
    case 258: return 5.0f; // iron_axe
    case 271: return 3.0f; // wooden_axe
    case 275: return 4.0f; // stone_axe
    case 279: return 6.0f; // diamond_axe
    case 286: return 3.0f; // golden_axe
    case 257: return 4.0f; // iron_pickaxe
    case 270: return 2.0f; // wooden_pickaxe
    case 274: return 3.0f; // stone_pickaxe
    case 278: return 5.0f; // diamond_pickaxe
    case 285: return 2.0f; // golden_pickaxe
    case 256: return 4.5f; // iron_shovel
    case 269: return 2.5f; // wooden_shovel
    case 273: return 3.5f; // stone_shovel
    case 277: return 5.5f; // diamond_shovel
    case 284: return 2.5f; // golden_shovel
    default: return 1.0f; // puno / item sin dano melee
    }
}

}
