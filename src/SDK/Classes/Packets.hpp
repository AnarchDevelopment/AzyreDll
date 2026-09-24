#pragma once

#include "Framework/Hook.hpp"
#include "Framework/Memory.hpp"
#include "SDK/Offsets.hpp"
#include "SDK/VTables.hpp"

namespace mc::packets {

struct UseItemPacket
{
    uintptr_t addr = 0;

    int x() const { return mem::read<int>(addr + off::useItemPacket::X); }
    int y() const { return mem::read<int>(addr + off::useItemPacket::Y); }
    int z() const { return mem::read<int>(addr + off::useItemPacket::Z); }
    uint8_t face() const { return mem::read<uint8_t>(addr + off::useItemPacket::Face); }
    int flag() const { return mem::read<int>(addr + off::useItemPacket::Flag); }
};

inline void* useItemVtableFunc(int slot)
{
    return hook::vtableFunc(vt::UseItemPacket, slot);
}

inline void* packetSenderSendFunc()
{
    return hook::vtableFunc(vt::PacketSender, vt::slot::PacketSender::Send);
}

}
