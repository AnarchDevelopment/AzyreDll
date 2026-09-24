#pragma once

#include "Framework/Memory.hpp"
#include "SDK/VTables.hpp"

namespace mc {

class BlockSource
{
public:
    BlockSource() = default;
    explicit BlockSource(uintptr_t a) : addr(a) {}

    bool valid() const
    {
        if (!addr || !mem::isReadable(addr, sizeof(uintptr_t)))
            return false;
        return mem::read<uintptr_t>(addr) == mem::resolve(vt::BlockSource);
    }
    uintptr_t address() const { return addr; }

private:
    uintptr_t addr = 0;
};

}
