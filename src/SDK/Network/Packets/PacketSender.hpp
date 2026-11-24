#pragma once

#include <cstdint>

namespace mc::packets {

class PacketSender
{
public:
    static constexpr uintptr_t VTableRva = 0xE699D0;
    static constexpr uintptr_t BatchedVTableRva = 0xE69A00;
    static constexpr int SendSlot = 2;
    static constexpr uintptr_t BatchedSendSlot = 2;
    static constexpr uintptr_t ClientConnectorOffset = 0xE8;
    static constexpr uintptr_t ConnectorSenderOffset = 0x98;

    // Sends through MinecraftClient -> NetworkConnector -> gameplay PacketSender.
    static bool sendGameplay(void* packet);

    // Sends through a previously resolved PacketSender/BatchedPacketSender instance.
    static bool send(uintptr_t sender, void* packet);
};

} // namespace mc::packets
