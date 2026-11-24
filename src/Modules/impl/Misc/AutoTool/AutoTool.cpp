#include "AutoTool.hpp"
#include "GUI/Widgets.hpp"

#include "Features/Raycast.hpp"
#include "Framework/Math.hpp"
#include "SDK/Core/Game.hpp"
#include "SDK/Entity/LocalPlayer.hpp"
#include "SDK/Inventory/Inventory.hpp"
#include "SDK/Inventory/ItemInstance/ItemInstance.hpp"
#include "SDK/Network/Packets/MobEquipmentPacket.hpp"

#include <imgui.h>
#include <windows.h>
#include <cmath>

namespace mc {

namespace {

// Mapeo bloque -> herramienta requerida. Ids legacy del build actual; los que
// no aparecen aqui no fuerzan cambio de herramienta (mejor no tocar nada que
// adivinar mal).
ToolKind requiredTool(int blockId)
{
    switch (blockId)
    {
    case off::blocks::Stone:
    case off::blocks::Cobblestone:
    case off::blocks::Sandstone:
    case off::blocks::GoldOre:
    case off::blocks::IronOre:
    case off::blocks::CoalOre:
    case off::blocks::LapisOre:
    case off::blocks::DiamondOre:
    case off::blocks::RedstoneOre:
    case off::blocks::EmeraldOre:
    case off::blocks::EnderChest:
    case off::blocks::AncientDebris:
    case off::blocks::CopperOre:
    case 41:  // gold block
    case 42:  // iron block
    case 49:  // obsidian
    case 57:  // diamond block
    case 87:  // netherrack
    case 89:  // glowstone
    case 152: // redstone block
    case 155: // quartz block
    case 172: // hardened clay
    case 173: // coal block
    case 174: // packed ice
        return ToolKind::Pickaxe;
    case off::blocks::Log:
    case off::blocks::Planks:
    case 54: // chest
        return ToolKind::Axe;
    case off::blocks::Grass:
    case off::blocks::Dirt:
    case off::blocks::Sand:
    case off::blocks::Gravel:
    case 60: // farmland
    case 82: // clay
    case 110: // mycelium
    case 243: // podzol
        return ToolKind::Shovel;
    case off::blocks::Leaves:
        return ToolKind::Hoe;
    default:
        return ToolKind::None;
    }
}

// Id numerico legacy de las herramientas -> tipo + tier (0 bajo, 3 alto).
// El set de ids es el clasico (wood/stone/iron/diamond/gold); si el build
// usa ids distintos basta con ajustar esta tabla.
void toolKindFromId(int itemId, ToolKind& kind, int& tier)
{
    kind = ToolKind::None;
    tier = -1;
    switch (itemId)
    {
    case 270: kind = ToolKind::Pickaxe; tier = 0; break;   // wooden
    case 274: kind = ToolKind::Pickaxe; tier = 1; break;   // stone
    case 257: kind = ToolKind::Pickaxe; tier = 2; break;   // iron
    case 278: kind = ToolKind::Pickaxe; tier = 3; break;   // diamond
    case 285: kind = ToolKind::Pickaxe; tier = 0; break;   // golden
    case 271: kind = ToolKind::Axe; tier = 0; break;
    case 275: kind = ToolKind::Axe; tier = 1; break;
    case 258: kind = ToolKind::Axe; tier = 2; break;
    case 279: kind = ToolKind::Axe; tier = 3; break;
    case 286: kind = ToolKind::Axe; tier = 0; break;
    case 269: kind = ToolKind::Shovel; tier = 0; break;
    case 273: kind = ToolKind::Shovel; tier = 1; break;
    case 256: kind = ToolKind::Shovel; tier = 2; break;
    case 277: kind = ToolKind::Shovel; tier = 3; break;
    case 284: kind = ToolKind::Shovel; tier = 0; break;
    case 290: kind = ToolKind::Hoe; tier = 0; break;
    case 291: kind = ToolKind::Hoe; tier = 1; break;
    case 292: kind = ToolKind::Hoe; tier = 2; break;
    case 293: kind = ToolKind::Hoe; tier = 3; break;
    case 294: kind = ToolKind::Hoe; tier = 0; break;
    default: break;
    }
}

} // namespace

AutoTool::AutoTool()
    : Module("AutoTool", "Switches to the best tool for the block you are mining",
             Category::Misc, 0)
{
    markHasSettings();
}

void AutoTool::onEnable()
{
    prevSlot_ = -1;
    serverSlot_ = -1;
}

void AutoTool::onDisable()
{
    // Restaurar la mano original: dejar la herramienta equipada tras apagar el
    // modulo es un cambio de inventario visible para el jugador y para el server.
    Game& game = Game::get();
    if (!game.localFound())
        return;
    LocalPlayer lp = game.localPlayer();
    if (!lp.valid())
        return;
    if (switchBack_ && prevSlot_ >= 0 && prevSlot_ <= 8 && prevSlot_ != lp.hotbarSlot())
        (void)switchTo(lp, prevSlot_);
    prevSlot_ = -1;
    serverSlot_ = -1;
}

bool AutoTool::switchTo(LocalPlayer& lp, int slot)
{
    if (slot < 0 || slot > 8)
        return false;

    lp.setHotbarSlot(slot);
    Inventory inv = lp.inventory();
    if (inv.valid() && inv.selectedSlot() != slot)
        inv.setSelectedSlot(slot);

    // El hand del cliente ya cambio; sin el paquete el servidor sigue viendo el
    // slot anterior y descarta las acciones (place/break) de la herramienta.
    if (syncServer_ && packets::sendMobEquipment(lp, slot))
        serverSlot_ = slot;
    else
        serverSlot_ = -1;
    return true;
}

int AutoTool::toolTierAt(const LocalPlayer& lp, int hotbarSlot, ToolKind& kind) const
{
    kind = ToolKind::None;
    Inventory inv = lp.inventory();
    if (!inv.valid())
        return -1;
    ItemInstance it(inv.hotbarItem(hotbarSlot));
    if (!it.valid() || it.count() <= 0)
        return -1;
    int tier = -1;
    toolKindFromId(it.itemId(), kind, tier);
    return tier;
}

void AutoTool::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;
    LocalPlayer lp = game.localPlayer();
    if (!lp.valid())
        return;

    bool mining = lp.leftClickFlag() || ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);

    if (onlyMining_)
    {
        if (!mining)
        {
            if (switchBack_ && prevSlot_ >= 0 && prevSlot_ <= 8 &&
                prevSlot_ != lp.hotbarSlot())
            {
                (void)switchTo(lp, prevSlot_);
                prevSlot_ = -1;
            }
            return;
        }
    }

    BlockSource bs = game.blockSource();
    if (!bs.valid())
        return;

    Vec3 eye = lp.eyePos();
    float yaw = lp.yaw();
    float pitch = lp.pitch();
    Vec3 dir{
        -std::sin(deg2rad(yaw)) * std::cos(deg2rad(pitch)),
        -std::sin(deg2rad(pitch)),
        std::cos(deg2rad(yaw)) * std::cos(deg2rad(pitch)),
    };

    RaycastHit hit = raycastBlock(bs, eye, dir, reach_);
    if (!hit.hit || hit.blockId <= 0)
        return;

    int want = bestToolSlot(lp, hit.blockId);
    if (want < 0 || want > 8)
        return;

    const int current = lp.hotbarSlot();
    if (current == want)
    {
        // La mano ya quedo sincronizada por nosotros: recordar que el server la
        // conoce para no reenviar el paquete en cada tick.
        if (serverSlot_ < 0 && syncServer_ && current >= 0)
            serverSlot_ = current;
        return;
    }

    // Mantener la herramienta actual si ya es del tipo correcto y no es peor
    // que la mejor encontrada: cambiar por un tier superior apenas acelera y
    // provoca parpadeo en el HUD.
    if (keepCurrent_)
    {
        ToolKind currentKind = ToolKind::None;
        const int currentTier = toolTierAt(lp, current, currentKind);
        const ToolKind need = requiredTool(hit.blockId);
        const bool usable = currentKind != ToolKind::None &&
                            (toolMode_ == 1 || need == ToolKind::None || currentKind == need);
        if (usable)
        {
            ToolKind wantKind = ToolKind::None;
            const int wantTier = toolTierAt(lp, want, wantKind);
            (void)wantKind;
            if (wantTier < 0 || currentTier >= wantTier)
                return;
        }
    }

    if (switchBack_ && prevSlot_ < 0)
        prevSlot_ = current;
    (void)switchTo(lp, want);
}

int AutoTool::bestToolSlot(const LocalPlayer& lp, int blockId)
{
    Inventory inv = lp.inventory();
    if (!inv.valid())
        return -1;

    ToolKind need = requiredTool(blockId);
    int best = -1;
    int bestTier = -1;

    for (int s = 0; s <= 8; ++s)
    {
        ItemInstance it(inv.hotbarItem(s));
        if (!it.valid() || it.count() <= 0)
            continue;

        ToolKind kind = ToolKind::None;
        int tier = -1;
        int id = it.itemId();
        if (id >= 0)
        {
            toolKindFromId(id, kind, tier);
        }
        else if (it.isToolVtable())
        {
            // Sin id legible: asumir la clase que necesitamos (o pickaxe).
            kind = need != ToolKind::None ? need : ToolKind::Pickaxe;
        }

        if (kind == ToolKind::None)
            continue;
        if (toolMode_ == 0 && need != ToolKind::None && kind != need)
            continue;

        if (tier > bestTier)
        {
            bestTier = tier;
            best = s;
        }
    }

    return best;
}

void AutoTool::drawSettings()
{
    const char* modes[] = {"Auto (block class)", "Any tool"};
    widgets::CCombo("Tool selection##at", &toolMode_, modes, 2);

    widgets::CSlider("Reach##at", &reach_, 3.0f, 6.0f, "%.1f");
    widgets::Checkbox("Only while mining##at", &onlyMining_);
    widgets::Checkbox("Switch back afterwards##at", &switchBack_);
    widgets::Checkbox("Keep current tool##at", &keepCurrent_);
    widgets::Checkbox("Sync server slot##at", &syncServer_);
}

MC_REGISTER_MODULE(AutoTool);

}