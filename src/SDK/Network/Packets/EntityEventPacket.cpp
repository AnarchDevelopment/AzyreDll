#include "EntityEventPacket.hpp"

#include "Framework/Hook.hpp"
#include "Framework/Log.hpp"
#include "Framework/Memory.hpp"

#include <windows.h>

#include <array>
#include <atomic>

namespace mc::packets {

namespace {

constexpr size_t kRingCapacity = 128;

struct HurtRecord
{
    std::atomic<uint64_t> sequence{0};
    std::atomic<uint64_t> runtimeId{0};
};

std::array<HurtRecord, kRingCapacity> g_hurtRecords{};
std::atomic<uint64_t> g_hurtSequence{0};
std::atomic<bool> g_trackingActive{false};
std::atomic<unsigned> g_traceCount{0};
bool g_hookInstalled = false;
void* g_hookTarget = nullptr;
using ReadFn = uint8_t(__fastcall*)(void*, void*);
ReadFn g_originalRead = nullptr;

uint8_t __fastcall readDetour(void* packet, void* stream)
{
    if (!g_originalRead)
        return 0;

    uint8_t readOk = 0;
    __try
    {
        readOk = g_originalRead(packet, stream);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }

    if (!readOk || !packet || !g_trackingActive.load(std::memory_order_relaxed))
        return readOk;

    uint64_t runtimeId = 0;
    uint8_t eventId = 0;
    int eventData = 0;
    const uintptr_t address = reinterpret_cast<uintptr_t>(packet);
    if (!mem::rawRead(&runtimeId, address + EntityEventPacket::RuntimeIdOffset,
                      sizeof(runtimeId)) ||
        !mem::rawRead(&eventId, address + EntityEventPacket::EventOffset,
                      sizeof(eventId)) ||
        !mem::rawRead(&eventData, address + EntityEventPacket::DataOffset,
                      sizeof(eventData)))
        return readOk;

    const unsigned traceIndex = g_traceCount.fetch_add(1, std::memory_order_relaxed);
    if (traceIndex < 24)
        MC_LOG("[THEventRead] rid=%llu event=%u data=%d read=%u",
               static_cast<unsigned long long>(runtimeId),
               static_cast<unsigned>(eventId), eventData,
               static_cast<unsigned>(readOk));

    if (eventId == static_cast<uint8_t>(EntityEventPacket::Event::Hurt) && runtimeId != 0)
    {
        const uint64_t sequence =
            g_hurtSequence.fetch_add(1, std::memory_order_relaxed) + 1;
        HurtRecord& record = g_hurtRecords[sequence % kRingCapacity];
        record.runtimeId.store(runtimeId, std::memory_order_relaxed);
        record.sequence.store(sequence, std::memory_order_release);
    }
    return readOk;
}

bool installHook()
{
    if (g_hookInstalled)
        return true;

    const uintptr_t target = mem::resolve(EntityEventPacket::ReadRva);
    if (!mem::isReadable(target, 16) ||
        !hook::create(reinterpret_cast<void*>(target),
                      reinterpret_cast<void*>(&readDetour),
                      reinterpret_cast<void**>(&g_originalRead)))
    {
        MC_LOG_ERROR("[Packet] Failed to hook EntityEventPacket::read");
        return false;
    }

    g_hookTarget = reinterpret_cast<void*>(target);
    g_hookInstalled = true;
    MC_LOG("[Packet] Hooked EntityEventPacket::read @ %p", g_hookTarget);
    return true;
}

} // namespace

bool EntityEventPacket::startHurtTracking()
{
    const bool installed = installHook();
    g_trackingActive.store(installed, std::memory_order_release);
    return installed;
}

void EntityEventPacket::setHurtTracking(bool active)
{
    if (active)
        startHurtTracking();
    else
        g_trackingActive.store(false, std::memory_order_release);
}

uint64_t EntityEventPacket::hurtSequence()
{
    return g_hurtSequence.load(std::memory_order_acquire);
}

unsigned EntityEventPacket::countHurtEvents(uint64_t runtimeId,
                                            uint64_t afterSequence,
                                            uint64_t& processedSequence)
{
    const uint64_t latest = hurtSequence();
    if (latest <= afterSequence)
    {
        processedSequence = afterSequence;
        return 0;
    }

    uint64_t first = afterSequence + 1;
    if (latest - first + 1 > kRingCapacity)
        first = latest - kRingCapacity + 1;

    unsigned count = 0;
    processedSequence = first - 1;
    for (uint64_t sequence = first; sequence <= latest; ++sequence)
    {
        const HurtRecord& record = g_hurtRecords[sequence % kRingCapacity];
        if (record.sequence.load(std::memory_order_acquire) != sequence)
            break;
        if (record.runtimeId.load(std::memory_order_relaxed) == runtimeId)
            ++count;
        processedSequence = sequence;
    }
    return count;
}

void EntityEventPacket::shutdownHurtTracking()
{
    g_trackingActive.store(false, std::memory_order_release);
    if (g_hookInstalled && g_hookTarget)
    {
        hook::remove(g_hookTarget);
        g_hookTarget = nullptr;
        g_originalRead = nullptr;
        g_hookInstalled = false;
    }
}

} // namespace mc::packets
