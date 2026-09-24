#pragma once

#include "Framework/Math.hpp"

#include <imgui.h>
#include <windows.h>
#include <unordered_map>

namespace mc::smooth {

inline Vec3 entityPos(uintptr_t addr, const Vec3& raw)
{
    struct Entry
    {
        Vec3 prevRaw{};
        Vec3 lastRaw{};
        ULONGLONG changeMs = 0;
        ULONGLONG seen = 0;
        bool valid = false;
    };
    static std::unordered_map<uintptr_t, Entry> s_state;
    static ULONGLONG s_lastClean = 0;

    ULONGLONG now = GetTickCount64();
    Entry& e = s_state[addr];

    if (!e.valid)
    {
        e.prevRaw = raw;
        e.lastRaw = raw;
        e.changeMs = now;
        e.valid = true;
    }
    else if (raw.x != e.lastRaw.x || raw.y != e.lastRaw.y || raw.z != e.lastRaw.z)
    {
        e.prevRaw = e.lastRaw;
        e.lastRaw = raw;
        e.changeMs = now;
    }
    e.seen = now;

    float t = (float)(now - e.changeMs) / 50.0f;
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;

    if (now - s_lastClean > 2000)
    {
        s_lastClean = now;
        for (auto it = s_state.begin(); it != s_state.end();)
        {
            if (now - it->second.seen > 3000)
                it = s_state.erase(it);
            else
                ++it;
        }
    }

    return {
        e.prevRaw.x + (e.lastRaw.x - e.prevRaw.x) * t,
        e.prevRaw.y + (e.lastRaw.y - e.prevRaw.y) * t,
        e.prevRaw.z + (e.lastRaw.z - e.prevRaw.z) * t,
    };
}

}
