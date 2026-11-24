#pragma once

#include <cstdint>

namespace mc::packets {

struct EntityEventPacket
{
    enum class Event : uint8_t
    {
        Hurt = 2,
    };

    static constexpr uintptr_t VTableRva = 0xE644C8;
    static constexpr uintptr_t ReadRva = 0x262C30;
    static constexpr int ReadSlot = 3;
    static constexpr uintptr_t RuntimeIdOffset = 0x10;
    static constexpr uintptr_t EventOffset = 0x18;
    static constexpr uintptr_t DataOffset = 0x1C;

    // The packet's enum is one byte (as seen in Solstice's ActorEventPacket
    // and confirmed by this build's byte serializer at +0x18).
    static bool startHurtTracking();
    static void setHurtTracking(bool active);
    static uint64_t hurtSequence();
    static unsigned countHurtEvents(uint64_t runtimeId, uint64_t afterSequence,
                                    uint64_t& processedSequence);
    static void shutdownHurtTracking();
};

} // namespace mc::packets
