#pragma once
#include <cstdint>
namespace mc::packets {
struct SetEntityMotionPacket
{
    static constexpr uintptr_t VTableRva = 0xE6A1A0;
    static constexpr int HandleSlot = 4;
    static constexpr uintptr_t EntityIdOffset = 0x08;
    static constexpr uintptr_t RuntimeIdOffset = 0x08;
    static constexpr uintptr_t VelocityXOffset = 0x18;
    static constexpr uintptr_t VelocityYOffset = 0x1C;
    static constexpr uintptr_t VelocityZOffset = 0x20;
};
}
