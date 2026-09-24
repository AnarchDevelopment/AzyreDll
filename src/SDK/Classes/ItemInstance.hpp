#pragma once

#include "Framework/Memory.hpp"
#include "SDK/Offsets.hpp"
#include "SDK/VTables.hpp"

#include <string>

namespace mc {

class ItemInstance
{
public:
    ItemInstance() = default;
    explicit ItemInstance(uintptr_t a) : addr(a) {}

    bool valid() const { return addr && mem::isReadable(addr, 0x30); }
    uintptr_t address() const { return addr; }

    int count() const { return mem::read<int>(addr + off::item::Count); }
    int metadata() const { return mem::read<int>(addr + off::item::Metadata); }
    uintptr_t item() const { return mem::read<uintptr_t>(addr + off::item::ItemPtr); }

    static bool isBlockItemVtable(uintptr_t itemVtable)
    {
        if (!itemVtable)
            return false;
        for (uintptr_t candidate : vt::blockItemVtables)
        {
            if (itemVtable == mem::resolve(candidate))
                return true;
        }
        return false;
    }

    bool isBlockItem() const
    {
        if (!valid())
            return false;
        return isBlockItemVtable(mem::read<uintptr_t>(item()));
    }

private:
    uintptr_t addr = 0;
};

}
