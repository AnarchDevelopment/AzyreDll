#pragma once

#include <cstdint>

namespace mc {
class Actor;
}

namespace mc::packets {

struct InteractPacket
{
    static constexpr uintptr_t VTableRva = 0xE44C40;
    static constexpr uintptr_t TypeOffset = 0x08;
    static constexpr uintptr_t FieldOffset = 0x0C;
    static constexpr uintptr_t ActionOffset = 0x10;
    static constexpr uintptr_t TargetRuntimeIdOffset = 0x18;
    static constexpr uint8_t AttackAction = 2;

    static bool sendAttack(const Actor& target);
};

} // namespace mc::packets
