#pragma once

#include <cstddef>
#include <cstdint>

namespace mc::off {

// Inventory and item instance layouts for the active game build.
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

namespace item
{
    inline constexpr uintptr_t IdCandidates[] = {0x08, 0x0C, 0x10, 0x14,
                                                 0x18, 0x1C, 0x20, 0x24};
    inline constexpr int IdMin = 0;
    inline constexpr int IdMax = 4096;
}

}
