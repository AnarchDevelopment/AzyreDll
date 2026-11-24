#include "HitFeed.hpp"

#include <array>
#include <atomic>
#include <cmath>

namespace mc::hitfeed {

namespace {

constexpr size_t kRingCapacity = 64;

struct HitRecord
{
    std::atomic<uint64_t> sequence{0};
    std::atomic<uint64_t> target{0};
    std::atomic<float> damage{0.0f};
};

// `sequence` se escribe al final: un lector que vea la secuencia nueva ya
// observa el resto de los campos (mismo patron que los ring buffers del SDK).
std::array<HitRecord, kRingCapacity> g_records{};
std::atomic<uint64_t> g_sequence{0};

} // namespace

void recordHit(uintptr_t targetAddr, float damage)
{
    if (!targetAddr || !std::isfinite(damage) || damage <= 0.0f)
        return;

    const uint64_t sequence = g_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    HitRecord& record = g_records[sequence % kRingCapacity];
    record.target.store(static_cast<uint64_t>(targetAddr), std::memory_order_relaxed);
    record.damage.store(damage, std::memory_order_relaxed);
    record.sequence.store(sequence, std::memory_order_release);
}

uint64_t sequence()
{
    return g_sequence.load(std::memory_order_acquire);
}

float consumeHits(uintptr_t targetAddr, uint64_t afterSequence,
                  uint64_t& processedSequence, unsigned& hitCount)
{
    hitCount = 0;
    const uint64_t latest = sequence();
    if (latest <= afterSequence)
    {
        processedSequence = afterSequence;
        return 0.0f;
    }

    uint64_t first = afterSequence + 1;
    if (latest - first + 1 > kRingCapacity)
        first = latest - kRingCapacity + 1;

    const uint64_t wanted = static_cast<uint64_t>(targetAddr);
    float total = 0.0f;
    processedSequence = first - 1;
    for (uint64_t current = first; current <= latest; ++current)
    {
        const HitRecord& record = g_records[current % kRingCapacity];
        if (record.sequence.load(std::memory_order_acquire) != current)
            break;
        processedSequence = current;
        if (record.target.load(std::memory_order_relaxed) != wanted)
            continue;
        const float damage = record.damage.load(std::memory_order_relaxed);
        if (damage > 0.0f)
        {
            total += damage;
            ++hitCount;
        }
    }
    return total;
}

} // namespace mc::hitfeed
