#pragma once

#include "Framework/Memory.hpp"

#include <cstdint>

namespace mc::packets {

class UseItemPacket
{
public:
    static constexpr uintptr_t VTableRva = 0xE6A020;
    static constexpr uintptr_t CtorRva = 0x5100F0;
    static constexpr uintptr_t CtorVariantRva = 0x510270;
    static constexpr uintptr_t ReadRva = 0x394CC0;
    static constexpr uintptr_t WriteRva = 0x394DB0;
    static constexpr uintptr_t GetNameRva = 0x394E90;

    static constexpr uintptr_t TypeOffset = 0x08;
    static constexpr uintptr_t XOffset = 0x10;
    static constexpr uintptr_t YOffset = 0x14;
    static constexpr uintptr_t ZOffset = 0x18;
    static constexpr uintptr_t FaceOffset = 0x1C;
    static constexpr uintptr_t PlayerXOffset = 0x20;
    static constexpr uintptr_t PlayerYOffset = 0x24;
    static constexpr uintptr_t PlayerZOffset = 0x28;
    static constexpr uintptr_t ClickXOffset = 0x2C;
    static constexpr uintptr_t ClickYOffset = 0x30;
    static constexpr uintptr_t ClickZOffset = 0x34;
    static constexpr uintptr_t ItemOffset = 0x38;
    static constexpr uintptr_t FlagOffset = 0x58;

    explicit UseItemPacket(uintptr_t address = 0) : address_(address) {}

    uintptr_t address() const { return address_; }
    int type() const { return mem::read<int>(address_ + TypeOffset); }
    int x() const { return mem::read<int>(address_ + XOffset); }
    int y() const { return mem::read<int>(address_ + YOffset); }
    int z() const { return mem::read<int>(address_ + ZOffset); }
    uint8_t face() const { return mem::read<uint8_t>(address_ + FaceOffset); }
    int flag() const { return mem::read<int>(address_ + FlagOffset); }

private:
    uintptr_t address_ = 0;
};

} // namespace mc::packets
