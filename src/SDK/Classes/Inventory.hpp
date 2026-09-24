#pragma once

#include "Framework/Memory.hpp"
#include "SDK/Offsets.hpp"
#include "SDK/VTables.hpp"

namespace mc {

class ItemInstance;

class Inventory
{
public:
    Inventory() = default;
    explicit Inventory(uintptr_t a) : addr(a) {}

    bool valid() const { return addr && mem::isReadable(addr, 0xB8); }
    uintptr_t address() const { return addr; }

    int selectedSlot() const { return mem::read<int>(addr + off::inv::SelectedSlot); }
    void setSelectedSlot(int slot) { mem::write<int>(addr + off::inv::SelectedSlot, slot); }

    size_t hotbarMapSize() const
    {
        uintptr_t begin = mem::read<uintptr_t>(addr + off::inv::HotbarMapBegin);
        uintptr_t end = mem::read<uintptr_t>(addr + off::inv::HotbarMapEnd);
        if (!begin || end <= begin)
            return 0;
        return (end - begin) / sizeof(int);
    }

    size_t itemCount() const
    {
        uintptr_t begin = mem::read<uintptr_t>(addr + off::inv::ItemsBegin);
        uintptr_t end = mem::read<uintptr_t>(addr + off::inv::ItemsEnd);
        if (!begin || end <= begin)
            return 0;
        return (end - begin) / sizeof(uintptr_t);
    }

    uintptr_t itemAt(int realSlot) const
    {
        uintptr_t begin = mem::read<uintptr_t>(addr + off::inv::ItemsBegin);
        uintptr_t end = mem::read<uintptr_t>(addr + off::inv::ItemsEnd);
        if (!begin || realSlot < 0)
            return 0;
        uintptr_t target = begin + (uintptr_t)realSlot * sizeof(uintptr_t);
        if (target + sizeof(uintptr_t) > end)
            return 0;
        return mem::read<uintptr_t>(target);
    }

    uintptr_t hotbarItem(int hotbarSlot) const
    {
        if (hotbarSlot < 0 || hotbarSlot > 8)
            return 0;
        uintptr_t mapBegin = mem::read<uintptr_t>(addr + off::inv::HotbarMapBegin);
        uintptr_t mapEnd = mem::read<uintptr_t>(addr + off::inv::HotbarMapEnd);
        if (!mapBegin)
            return 0;
        uintptr_t entry = mapBegin + (uintptr_t)hotbarSlot * sizeof(int);
        if (entry + sizeof(int) > mapEnd)
            return itemAt(hotbarSlot);
        int realSlot = mem::read<int>(entry);
        return itemAt(realSlot);
    }

private:
    uintptr_t addr = 0;
};

}
