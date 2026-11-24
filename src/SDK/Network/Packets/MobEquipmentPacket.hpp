#pragma once

namespace mc {

class LocalPlayer;

namespace packets {

struct MobEquipmentPacket
{
    static constexpr uintptr_t VTableRva = 0xE64528;
};

// Send the selected hotbar slot and its current ItemStack through the live
// gameplay PacketSender (MobEquipmentPacket, packet type 27).
bool sendMobEquipment(const LocalPlayer& player, int hotbarSlot);

} // namespace packets
} // namespace mc
