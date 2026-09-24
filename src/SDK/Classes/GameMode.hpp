#pragma once

#include "Framework/Memory.hpp"
#include "Framework/Math.hpp"
#include "SDK/Offsets.hpp"
#include "SDK/VTables.hpp"
#include "SDK/Classes/ItemInstance.hpp"

namespace mc {

class GameMode
{
public:
    GameMode() = default;
    explicit GameMode(uintptr_t a) : addr(a) {}

    bool valid() const
    {
        if (!addr || !mem::isReadable(addr, sizeof(uintptr_t)))
            return false;
        uintptr_t v = mem::read<uintptr_t>(addr);
        return v == mem::resolve(vt::CreativeMode) ||
               v == mem::resolve(vt::SurvivalMode) ||
               v == mem::resolve(vt::GameMode);
    }
    uintptr_t address() const { return addr; }

    bool useItemOn(const ItemInstance& item, const BlockPos& pos, uint8_t face,
                   const Vec3& click, uintptr_t playerAddr);
    bool useItemOnVt(const ItemInstance& item, const BlockPos& pos, uint8_t face,
                     const Vec3& click, uintptr_t playerAddr);

private:
    uintptr_t addr = 0;
};

bool blockItemUseOn(const ItemInstance& item, const BlockPos& pos, uint8_t face,
                    const Vec3& click, uintptr_t playerAddr);

}
