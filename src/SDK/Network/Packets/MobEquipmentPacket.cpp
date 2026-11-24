#include "MobEquipmentPacket.hpp"

#include "Framework/Memory.hpp"
#include "SDK/Inventory/Inventory.hpp"
#include "SDK/Entity/LocalPlayer.hpp"
#include "SDK/Core/Game.hpp"
#include "SDK/Engine/Memory/Offsets.hpp"
#include "SDK/Network/Packets/PacketSender.hpp"

#include <cstdint>
#include <cstring>

namespace mc::packets {

namespace {

constexpr size_t kPacketBytes = 0x40;
constexpr size_t kItemStackOffset = 0x18;
constexpr size_t kItemStackBytes = 0x20;
constexpr size_t kSlotOffset = 0x38;
constexpr size_t kSelectedSlotOffset = 0x39;
constexpr size_t kContainerIdOffset = 0x3A;

void destroyItemStackCopy(uintptr_t stackAddress)
{
    // ItemStack's copy constructor creates the shared backing object at +0x08.
    // The game's native MobEquipmentPacket path releases it via vtable slot 0.
    uintptr_t owner = 0;
    if (!mem::rawRead(&owner, stackAddress + sizeof(uintptr_t), sizeof(owner)) ||
        !owner || !mem::isReadable(owner, sizeof(uintptr_t)))
        return;

    const uintptr_t ownerVtable = mem::read<uintptr_t>(owner);
    if (!ownerVtable || !mem::isReadable(ownerVtable, sizeof(uintptr_t)))
        return;

    const uintptr_t destroy = mem::read<uintptr_t>(ownerVtable);
    if (!destroy)
        return;

    __try
    {
        using DestroyFn = void(__fastcall*)(void*, int);
        reinterpret_cast<DestroyFn>(destroy)(reinterpret_cast<void*>(owner), 1);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

} // namespace

bool sendMobEquipment(const LocalPlayer& player, int hotbarSlot)
{
    if (!player.valid() || hotbarSlot < 0 || hotbarSlot > 8)
        return false;

    const uintptr_t runtimeIdAddress = player.address() + off::player::RuntimeID;
    if (!mem::isReadable(runtimeIdAddress, sizeof(uint64_t)))
        return false;
    const uint64_t runtimeId = mem::read<uint64_t>(runtimeIdAddress);
    if (runtimeId == 0 || runtimeId == UINT64_MAX)
        return false;

    const Inventory inventory = player.inventory();
    if (!inventory.valid())
        return false;

    const uintptr_t stackSource = inventory.hotbarItem(hotbarSlot);
    if (!stackSource || !mem::isReadable(stackSource, kItemStackBytes))
        return false;

    int realSlot = inventory.hotbarRealSlot(hotbarSlot);
    if (realSlot < 0 && inventory.hotbarMapSize() < 9)
        realSlot = hotbarSlot;
    if (realSlot < 0 || realSlot > 255)
        return false;

    const uintptr_t copyCtor = mem::resolve(off::fn::ItemStackCopy);
    const uintptr_t packetVtable = mem::resolve(MobEquipmentPacket::VTableRva);
    if (!copyCtor || !packetVtable)
        return false;

    // MobEquipmentPacket serializes runtime ID, ItemStack (with container ID),
    // inventory slot and hotbar slot. Container ID 0 is the player's inventory.
    alignas(16) uint8_t packet[kPacketBytes] = {};
    const uint64_t header = 0x100000002ull; // type=2, field=1
    std::memcpy(packet, &packetVtable, sizeof(packetVtable));
    std::memcpy(packet + 0x08, &header, sizeof(header));
    std::memcpy(packet + 0x10, &runtimeId, sizeof(runtimeId));

    bool copied = false;
    __try
    {
        using ItemStackCopyFn = void*(__fastcall*)(void*, void*);
        copied = reinterpret_cast<ItemStackCopyFn>(copyCtor)(
                     packet + kItemStackOffset,
                     reinterpret_cast<void*>(stackSource)) == packet + kItemStackOffset;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        copied = false;
    }
    if (!copied)
        return false;

    packet[kSlotOffset] = static_cast<uint8_t>(realSlot);
    packet[kSelectedSlotOffset] = static_cast<uint8_t>(hotbarSlot);
    packet[kContainerIdOffset] = 0;

    const bool sent = PacketSender::sendGameplay(packet);
    destroyItemStackCopy(reinterpret_cast<uintptr_t>(packet + kItemStackOffset));
    return sent;
}

} // namespace mc::packets
