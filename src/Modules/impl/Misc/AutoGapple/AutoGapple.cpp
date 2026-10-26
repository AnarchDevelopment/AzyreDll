#include "AutoGapple.hpp"

#include "GUI/Widgets.hpp"
#include "SDK/Game.hpp"
#include "SDK/Offsets.hpp"
#include "SDK/VTables.hpp"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <excpt.h>

namespace mc {

namespace {

constexpr int kGoldenAppleId = 322;
constexpr int kNormalAppleMetadata = 0;
constexpr ULONGLONG kUseStartGraceMs = 250;
constexpr ULONGLONG kUseFallbackMs = 2500;
constexpr ULONGLONG kAttemptCooldownMs = 1000;

struct alignas(16) MobEquipmentPacket
{
    uintptr_t vtable;
    uint32_t type;
    uint32_t field;
    uint64_t runtimeId;
    uint8_t itemStack[0x20];
    uint8_t inventorySlot;
    uint8_t hotbarSlot;
    uint8_t containerId;
    uint8_t padding[5];
};

static_assert(sizeof(MobEquipmentPacket) == 0x40);
static_assert(offsetof(MobEquipmentPacket, runtimeId) == 0x10);
static_assert(offsetof(MobEquipmentPacket, itemStack) == 0x18);
static_assert(offsetof(MobEquipmentPacket, inventorySlot) == 0x38);

bool sendMobEquipment(uintptr_t player, const ItemInstance& item, int realSlot,
                      int hotbarSlot)
{
    if (!player || !item.valid() || realSlot < 0 || realSlot > 255 ||
        hotbarSlot < 0 || hotbarSlot > 8 ||
        !mem::isReadable(player + off::player::RuntimeID, sizeof(uint64_t)))
        return false;

    const uint64_t runtimeId = mem::read<uint64_t>(player + off::player::RuntimeID);
    const uintptr_t packetVtable = mem::resolve(vt::MobEquipmentPacket);
    const uintptr_t copyStack = mem::resolve(off::fn::ItemStackCopy);
    if (runtimeId == 0 || runtimeId == UINT64_MAX || !packetVtable || !copyStack)
        return false;

    const uintptr_t client = Game::get().clientAddr();
    if (!client || !mem::isReadable(client + off::chain::ClientNetworkConnector,
                                     sizeof(uintptr_t)))
        return false;
    const uintptr_t connector =
        mem::read<uintptr_t>(client + off::chain::ClientNetworkConnector);
    if (!connector || !mem::isReadable(
                          connector + off::chain::NetworkConnectorPacketSender,
                          sizeof(uintptr_t)))
        return false;
    const uintptr_t sender =
        mem::read<uintptr_t>(connector + off::chain::NetworkConnectorPacketSender);
    if (!sender || !mem::isReadable(sender, sizeof(uintptr_t)))
        return false;

    const uintptr_t senderVtable = mem::read<uintptr_t>(sender);
    if (senderVtable != mem::resolve(vt::PacketSender) &&
        senderVtable != mem::resolve(vt::BatchedPacketSender))
        return false;

    constexpr int sendSlot = vt::slot::PacketSender::Send;
    const uintptr_t sendEntry = senderVtable + (uintptr_t)sendSlot * sizeof(uintptr_t);
    if (!mem::isReadable(sendEntry, sizeof(uintptr_t)))
        return false;
    const uintptr_t sendFn = mem::read<uintptr_t>(sendEntry);
    if (!sendFn)
        return false;

    MobEquipmentPacket packet{};
    packet.vtable = packetVtable;
    packet.type = 2;
    packet.field = 1;
    packet.runtimeId = runtimeId;
    packet.inventorySlot = static_cast<uint8_t>(realSlot);
    packet.hotbarSlot = static_cast<uint8_t>(hotbarSlot);
    packet.containerId = 0;

    bool sent = false;
    __try
    {
        using CopyStackFn = void*(__fastcall*)(void*, const void*);
        reinterpret_cast<CopyStackFn>(copyStack)(packet.itemStack,
                                                 reinterpret_cast<const void*>(item.address()));
        using PacketSenderSendFn = void(__fastcall*)(void*, void*);
        reinterpret_cast<PacketSenderSendFn>(sendFn)(
            reinterpret_cast<void*>(sender), &packet);
        sent = true;

        // ItemStack's shared backing object is released the same way the
        // game's LocalPlayer tick releases its stack-built equipment packet.
        const uintptr_t shared =
            *reinterpret_cast<const uintptr_t*>(packet.itemStack + sizeof(uintptr_t));
        if (shared && mem::isReadable(shared, sizeof(uintptr_t)))
        {
            const uintptr_t sharedVtable = mem::read<uintptr_t>(shared);
            if (sharedVtable && mem::isReadable(sharedVtable, sizeof(uintptr_t)))
            {
                const uintptr_t destructor = mem::read<uintptr_t>(sharedVtable);
                if (destructor)
                {
                    using SharedReleaseFn = void(__fastcall*)(void*, int);
                    reinterpret_cast<SharedReleaseFn>(destructor)(
                        reinterpret_cast<void*>(shared), 1);
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        sent = false;
    }
    return sent;
}

} // namespace

AutoGapple::AutoGapple()
    : Module("AutoGapple",
             "Automatically uses a golden apple when health reaches the configured threshold",
             Category::Misc, 0)
{
    markHasSettings();
}

void AutoGapple::onEnable()
{
    clearOverride();
    nextAttempt_ = 0;
    status_ = "Waiting for health threshold";
}

void AutoGapple::onDisable()
{
    restoreServerSlot();
    clearOverride();
}

int AutoGapple::findAppleSlot() const
{
    const LocalPlayer player = Game::get().localPlayer();
    if (!player.isLocal())
        return -1;
    Inventory inventory = player.inventory();
    if (!inventory.valid())
        return -1;

    int enchantedSlot = -1;
    for (int slot = 0; slot < 9; ++slot)
    {
        const ItemInstance item(inventory.hotbarItem(slot));
        // ItemInstance::count() is not confirmed for this build. Do not use it
        // as an exclusion gate: stale/incorrect reads would hide valid apples.
        if (!item.valid() || item.itemId() != kGoldenAppleId)
            continue;

        const int metadata = item.metadata();
        if (metadata == kNormalAppleMetadata)
            return slot;
        // Metadata is a separate, unverified ItemStack field in this build.
        // With enchanted apples enabled, accept any non-normal variant as a
        // fallback rather than risking a false negative from that accessor.
        if (useEnchantedApples_ && metadata != kNormalAppleMetadata &&
            enchantedSlot < 0)
            enchantedSlot = slot;
    }
    return enchantedSlot;
}

bool AutoGapple::sendEquipment(int hotbarSlot)
{
    const LocalPlayer player = Game::get().localPlayer();
    if (!player.isLocal() || hotbarSlot < 0 || hotbarSlot > 8)
        return false;

    const Inventory inventory = player.inventory();
    if (!inventory.valid())
        return false;
    const ItemInstance item(inventory.hotbarItem(hotbarSlot));
    const int realSlot = inventory.hotbarRealSlot(hotbarSlot);
    return sendMobEquipment(player.address(), item, realSlot, hotbarSlot);
}

void AutoGapple::restoreServerSlot()
{
    if (overrideSlot_ < 0)
        return;

    const LocalPlayer player = Game::get().localPlayer();
    if (player.isLocal())
    {
        const int visibleSlot = player.hotbarSlot();
        if (visibleSlot >= 0 && visibleSlot <= 8)
            sendEquipment(visibleSlot);
    }
}

void AutoGapple::clearOverride()
{
    overrideSlot_ = -1;
    originalUserSlot_ = -1;
    overrideStarted_ = 0;
    sawUseAnimation_ = false;
}

void AutoGapple::onTick()
{
    const ULONGLONG now = GetTickCount64();
    const LocalPlayer player = Game::get().localPlayer();
    if (!player.isLocal())
    {
        status_ = "Waiting for local player";
        if (overrideSlot_ >= 0)
        {
            restoreServerSlot();
            clearOverride();
        }
        return;
    }

    Inventory inventory = player.inventory();
    if (!inventory.valid())
    {
        status_ = "Inventory unavailable";
        return;
    }

    if (overrideSlot_ >= 0)
    {
        const bool usingItem = player.isUsingItem();
        sawUseAnimation_ = sawUseAnimation_ || usingItem;
        const ULONGLONG elapsed = now - overrideStarted_;
        const bool userChangedSlot = player.hotbarSlot() != originalUserSlot_;
        const bool failedToStart = elapsed >= kUseStartGraceMs && !sawUseAnimation_;
        const bool useFinished = elapsed >= kUseStartGraceMs && sawUseAnimation_ && !usingItem;
        const bool timedOut = elapsed >= kUseFallbackMs;
        if (userChangedSlot || failedToStart || useFinished || timedOut)
        {
            restoreServerSlot();
            clearOverride();
            nextAttempt_ = now + kAttemptCooldownMs;
            status_ = userChangedSlot ? "Restored after hotbar change"
                    : failedToStart ? "Use animation did not start; restored slot"
                    : timedOut ? "Use timed out; restored slot"
                               : "Use finished; restored slot";
        }
        else
            status_ = "Using golden apple...";
        return;
    }

    if (now < nextAttempt_)
    {
        status_ = "Retry cooldown";
        return;
    }
    if (player.isUsingItem())
    {
        status_ = "Player is already using an item";
        return;
    }

    const float health = player.currentHealth();
    if (health <= 0.0f)
    {
        status_ = "Health attribute unavailable";
        return;
    }
    if (health > healthThreshold_)
    {
        status_ = "Health is above threshold";
        return;
    }

    const int appleSlot = findAppleSlot();
    const int userSlot = player.hotbarSlot();
    if (appleSlot < 0)
    {
        status_ = "No golden apple detected in hotbar";
        return;
    }
    if (userSlot < 0 || userSlot > 8)
    {
        status_ = "Visible hotbar slot is invalid";
        return;
    }

    const ItemInstance apple(inventory.hotbarItem(appleSlot));
    if (!apple.valid() || !sendEquipment(appleSlot))
    {
        status_ = "MobEquipment packet could not be sent";
        nextAttempt_ = now + kAttemptCooldownMs;
        return;
    }

    // GameMode::useItem reads Inventory::selectedSlot for its UseItemPacket.
    // Change it only during the native call; the rendered/local hotbar slot
    // at LocalPlayer + 0x10C0 is never touched.
    const int previousSelectedSlot = inventory.selectedSlot();
    inventory.setSelectedSlot(appleSlot);
    // The native return value reports whether the ItemStack changed, not
    // whether a use action started. Consumables decrement after their use
    // duration, so a false result is normal while a golden apple is being eaten.
    const bool stackChanged = Game::get().gameMode().useItem(apple, player.address());
    inventory.setSelectedSlot(previousSelectedSlot);

    overrideSlot_ = appleSlot;
    originalUserSlot_ = userSlot;
    overrideStarted_ = now;
    sawUseAnimation_ = player.isUsingItem();
    status_ = sawUseAnimation_ ? "Using golden apple..."
                               : stackChanged ? "Item changed; waiting for use state"
                                              : "Use requested; waiting for use state";
}

void AutoGapple::drawSettings()
{
    widgets::CSlider("Health threshold##autogapple", &healthThreshold_, 1.0f, 20.0f,
                     "%.1f HP");
    widgets::Checkbox("Use enchanted apples##autogapple", &useEnchantedApples_);
    ImGui::TextDisabled("Prefers normal golden apples when both are available.");
    const LocalPlayer player = Game::get().localPlayer();
    if (player.isLocal())
    {
        ImGui::TextDisabled("Current health: %.1f HP", player.currentHealth());
        const int appleSlot = findAppleSlot();
        if (appleSlot >= 0)
            ImGui::TextDisabled("Golden apple detected in slot %d", appleSlot + 1);
        else
            ImGui::TextDisabled("No golden apple detected in hotbar");
    }
    ImGui::TextDisabled("Status: %s", status_);
    if (overrideSlot_ >= 0)
        ImGui::TextDisabled("Server equipment slot: %d", overrideSlot_ + 1);
}

nlohmann::json AutoGapple::saveSettings() const
{
    return nlohmann::json{{"healthThreshold", healthThreshold_},
                          {"useEnchantedApples", useEnchantedApples_}};
}

void AutoGapple::loadSettings(const nlohmann::json& j)
{
    healthThreshold_ = j.value("healthThreshold", healthThreshold_);
    if (!std::isfinite(healthThreshold_))
        healthThreshold_ = 10.0f;
    healthThreshold_ = std::clamp(healthThreshold_, 1.0f, 20.0f);
    useEnchantedApples_ = j.value("useEnchantedApples", useEnchantedApples_);
}

MC_REGISTER_MODULE(AutoGapple);

} // namespace mc
