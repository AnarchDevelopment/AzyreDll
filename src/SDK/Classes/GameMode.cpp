#include "GameMode.hpp"

#include "Framework/Hook.hpp"
#include "SDK/VTables.hpp"

#include <excpt.h>

namespace mc {

using UseItemOnFn = bool(__fastcall*)(void* gameMode, void* itemInstance, void* player,
                                      int x, int y, int z, unsigned char face,
                                      unsigned int clickX, unsigned int clickY, unsigned int clickZ);

static bool callUseItemOn(uintptr_t fnAddr, uintptr_t gameMode, const ItemInstance& item,
                          const BlockPos& pos, uint8_t face, const Vec3& click, uintptr_t playerAddr)
{
    if (!fnAddr || !gameMode || !item.valid() || !playerAddr)
        return false;

    auto fn = reinterpret_cast<UseItemOnFn>(fnAddr);
    bool ok = false;
    __try
    {
        ok = fn(reinterpret_cast<void*>(gameMode),
                reinterpret_cast<void*>(item.address()),
                reinterpret_cast<void*>(playerAddr),
                pos.x, pos.y, pos.z,
                face,
                floatBits(click.x), floatBits(click.y), floatBits(click.z));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return ok;
}

bool GameMode::useItemOn(const ItemInstance& item, const BlockPos& pos, uint8_t face,
                         const Vec3& click, uintptr_t playerAddr)
{
    return callUseItemOn(mem::resolve(off::fn::GameModeUseItemOnWrapper),
                         addr, item, pos, face, click, playerAddr);
}

bool GameMode::useItemOnVt(const ItemInstance& item, const BlockPos& pos, uint8_t face,
                           const Vec3& click, uintptr_t playerAddr)
{
    void* fn = hook::vtableFunc(vt::GameMode, vt::slot::GameMode::UseItemOn);
    return callUseItemOn(reinterpret_cast<uintptr_t>(fn),
                         addr, item, pos, face, click, playerAddr);
}

bool blockItemUseOn(const ItemInstance& item, const BlockPos& pos, uint8_t face,
                    const Vec3& click, uintptr_t playerAddr)
{
    if (!item.valid() || !playerAddr)
        return false;

    uintptr_t itemPtr = item.item();
    if (!itemPtr)
        return false;

    using Fn = bool(__fastcall*)(void* item, void* itemInstance, void* player,
                                 int x, int y, int z, unsigned char face,
                                 unsigned int clickX, unsigned int clickY, unsigned int clickZ);
    auto fn = reinterpret_cast<Fn>(mem::resolve(off::fn::BlockItemUseOn));
    bool ok = false;
    __try
    {
        ok = fn(reinterpret_cast<void*>(itemPtr),
                reinterpret_cast<void*>(item.address()),
                reinterpret_cast<void*>(playerAddr),
                pos.x, pos.y, pos.z,
                face,
                floatBits(click.x), floatBits(click.y), floatBits(click.z));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return ok;
}

}
