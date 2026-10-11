#include "AutoTool.hpp"
#include "GUI/Widgets.hpp"

#include "Features/Raycast.hpp"
#include "Framework/Math.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>
#include <cmath>

namespace mc {

namespace {

// Mapeo bloque -> herramienta requerida.
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
        return ToolKind::Pickaxe;
    case off::blocks::Log:
    case off::blocks::Planks:
        return ToolKind::Axe;
    case off::blocks::Grass:
    case off::blocks::Dirt:
    case off::blocks::Sand:
    case off::blocks::Gravel:
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
            if (switchBack_ && prevSlot_ >= 0 && prevSlot_ <= 8)
            {
                lp.setHotbarSlot(prevSlot_);
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
    if (lp.hotbarSlot() == want)
        return;

    if (switchBack_ && prevSlot_ < 0)
        prevSlot_ = lp.hotbarSlot();
    lp.setHotbarSlot(want);
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
}

MC_REGISTER_MODULE(AutoTool);

}