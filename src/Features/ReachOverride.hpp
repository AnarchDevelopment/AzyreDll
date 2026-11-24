#pragma once

namespace mc::reach_override {

enum class Owner
{
    KillAura,
    ReachModule,
};

// Multiple features can request a native reach value. The highest active
// request wins; the original game value is restored when the last owner clears.
void update(Owner owner, float reach);
void clear(Owner owner);

} // namespace mc::reach_override
