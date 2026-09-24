#pragma once

#include "Framework/Memory.hpp"
#include "Framework/Math.hpp"
#include "SDK/Offsets.hpp"
#include "SDK/Classes/Actor.hpp"
#include "SDK/Classes/Inventory.hpp"
#include "SDK/Classes/ItemInstance.hpp"

namespace mc {

class LocalPlayer : public Actor
{
public:
    LocalPlayer() = default;
    explicit LocalPlayer(uintptr_t a) : Actor(a) {}

    uintptr_t inventoryAddr() const { return mem::read<uintptr_t>(addr + off::player::InventoryPtr); }
    Inventory inventory() const { return Inventory(inventoryAddr()); }

    uintptr_t clientAddr() const { return mem::read<uintptr_t>(addr + off::player::MinecraftClientPtr); }
    uintptr_t moveInputAddr() const { return mem::read<uintptr_t>(addr + off::player::MoveInputPtr); }
    uintptr_t blockSourceAltAddr() const { return mem::read<uintptr_t>(addr + off::player::BlockSourceAlt); }

    int hotbarSlot() const { return mem::read<int>(addr + off::player::HotbarSlot); }
    void setHotbarSlot(int slot) { mem::write<int>(addr + off::player::HotbarSlot, slot); }

    bool leftClickFlag() const { return mem::read<uint8_t>(addr + off::player::LeftClickFlag) != 0; }
    void setLeftClickFlag(bool v) { mem::write<uint8_t>(addr + off::player::LeftClickFlag, v ? 1 : 0); }

    bool isUsingItem() const { return mem::read<uint8_t>(addr + off::player::IsUsingItem) != 0; }

    uintptr_t heldItemAddr() const
    {
        Inventory inv = inventory();
        if (!inv.valid())
            return 0;
        return inv.hotbarItem(hotbarSlot());
    }

    ItemInstance heldItem() const { return ItemInstance(heldItemAddr()); }
};

}
