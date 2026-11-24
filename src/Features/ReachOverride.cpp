#include "ReachOverride.hpp"

#include "Framework/Memory.hpp"
#include "SDK/Engine/Memory/Offsets.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace mc::reach_override {
namespace {

std::unordered_map<Owner, float> requests;
uintptr_t address = 0;
float originalReach = 3.0f;
bool originalValid = false;

void apply()
{
    if (!address || !mem::isReadable(address, sizeof(float)))
        return;

    if (requests.empty())
    {
        if (originalValid)
            mem::writeImage(address, originalReach);
        address = 0;
        originalValid = false;
        return;
    }

    float desired = originalValid ? originalReach : 3.0f;
    for (const auto& [owner, reach] : requests)
    {
        (void)owner;
        desired = std::max(desired, reach);
    }
    mem::writeImage(address, desired);
}

} // namespace

void update(Owner owner, float reach)
{
    if (!std::isfinite(reach))
        return;

    const uintptr_t resolved = mem::resolve(off::misc::Reach);
    if (!resolved || !mem::isReadable(resolved, sizeof(float)))
        return;

    if (!address || resolved != address)
    {
        address = resolved;
        originalValid = mem::rawRead(&originalReach, address, sizeof(originalReach)) &&
            std::isfinite(originalReach) && originalReach >= 0.0f && originalReach <= 64.0f;
        if (!originalValid)
            originalReach = 3.0f;
    }

    requests[owner] = std::clamp(reach, 1.0f, 15.0f);
    apply();
}

void clear(Owner owner)
{
    requests.erase(owner);
    apply();
}

} // namespace mc::reach_override
