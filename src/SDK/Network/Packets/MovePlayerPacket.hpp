#pragma once

#include "Framework/Memory.hpp"

#include <cstdint>

namespace mc::packets {

class MovePlayerPacket
{
public:
    static constexpr uintptr_t VTableRva = 0xE644F8;
    static constexpr uintptr_t RuntimeIdOffset = 0x08;
    static constexpr uintptr_t XOffset = 0x18;
    static constexpr uintptr_t YOffset = 0x1C;
    static constexpr uintptr_t ZOffset = 0x20;
    static constexpr uintptr_t PitchOffset = 0x24;
    static constexpr uintptr_t YawOffset = 0x28;
    static constexpr uintptr_t HeadYawOffset = 0x2C;
    static constexpr uintptr_t ModeOffset = 0x30;
    static constexpr uintptr_t CopySize = 0x60;
    static constexpr bool LayoutConfirmed = true;

    explicit MovePlayerPacket(uintptr_t address = 0) : address_(address) {}
    uintptr_t address() const { return address_; }
    float x() const { return mem::read<float>(address_ + XOffset); }
    float y() const { return mem::read<float>(address_ + YOffset); }
    float z() const { return mem::read<float>(address_ + ZOffset); }
    float pitch() const { return mem::read<float>(address_ + PitchOffset); }
    float yaw() const { return mem::read<float>(address_ + YawOffset); }
    float headYaw() const { return mem::read<float>(address_ + HeadYawOffset); }
    void setPitch(float value) const { mem::write<float>(address_ + PitchOffset, value); }
    void setYaw(float value) const { mem::write<float>(address_ + YawOffset, value); }
    void setHeadYaw(float value) const { mem::write<float>(address_ + HeadYawOffset, value); }

private:
    uintptr_t address_ = 0;
};

} // namespace mc::packets
