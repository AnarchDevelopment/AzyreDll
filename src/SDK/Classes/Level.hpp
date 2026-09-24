#pragma once

#include "Framework/Memory.hpp"
#include "SDK/Offsets.hpp"
#include "SDK/VTables.hpp"

namespace mc {

class Level
{
public:
    Level() = default;
    explicit Level(uintptr_t a) : addr(a) {}

    bool valid() const
    {
        if (!addr || !mem::isReadable(addr, sizeof(uintptr_t)))
            return false;
        uintptr_t v = mem::read<uintptr_t>(addr);
        for (uintptr_t candidate : vt::levelVtables)
        {
            if (v == mem::resolve(candidate))
                return true;
        }
        return false;
    }
    uintptr_t address() const { return addr; }

    uintptr_t blockSourceAddr() const { return mem::read<uintptr_t>(addr + off::chain::LevelBlockSource); }
    uintptr_t networkHandlerAddr() const { return mem::read<uintptr_t>(addr + off::chain::LevelNetworkHandler); }

private:
    uintptr_t addr = 0;
};

}
