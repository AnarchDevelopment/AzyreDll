#include "Scaffold.hpp"
#include "GUI/Widgets.hpp"

#include "Features/PlaceStats.hpp"
#include "Framework/Log.hpp"
#include "Framework/Math.hpp"
#include "Input/SendClick.hpp"
#include "SDK/Classes/MoveInputHandler.hpp"
#include "SDK/Game.hpp"

#include <excpt.h>
#include <imgui.h>
#include <unordered_map>

namespace mc {

Scaffold::Scaffold()
    : Module("Scaffold", "Places blocks under the player", Category::Movement, 'V')
{
    markHasSettings();
}

void Scaffold::onEnable()
{
    platformYSolid_ = false;
    lastPlaceTick_ = 0;
}

void Scaffold::onDisable()
{
    platformYSolid_ = false;
    if (physPhase_ > 0 && Game::get().localFound())
    {
        if (physPhase_ == 1)
            sendRightUp();
        Game::get().localPlayer().setPitch(physSavedPitch_);
    }
    physPhase_ = 0;
    physTimer_ = 0;
    physCooldown_ = 0;
}

void Scaffold::startPhys(LocalPlayer& local)
{
    physSavedPitch_ = local.pitch();

    float h = vecLength2D(local.velocity());
    float angle = (h > 0.05f && local.onGround()) ? bridgeAngle_ : 89.0f;
    local.setPitch(angle);
    sendRightDown();

    physPhase_ = 1;
    physTimer_ = 1;

    BlockPos b = blockFromVec(local.pos());
    placestats::lastX = b.x;
    placestats::lastY = b.y - 2;
    placestats::lastZ = b.z;
}

void Scaffold::advancePhys(LocalPlayer& local)
{
    if (--physTimer_ > 0)
        return;

    if (physPhase_ == 1)
    {
        sendRightUp();
        local.setPitch(physSavedPitch_);
        physPhase_ = 2;
        physTimer_ = 1;
        return;
    }

    physPhase_ = 0;
    physCooldown_ = (int)(retryMs_ / 50.0f);
    if (physCooldown_ < 2)
        physCooldown_ = 2;
    placestats::attempts++;
    placestats::lastOk = true;
}

static bool sehUseItemOn(GameMode& mode, const ItemInstance& item, const BlockPos& target,
                         uint8_t face, const Vec3& click, uintptr_t player)
{
    bool ok = false;
    __try
    {
        ok = mode.useItemOn(item, target, face, click, player);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }
    return ok;
}

static bool sehUseItemOnVt(GameMode& mode, const ItemInstance& item, const BlockPos& target,
                           uint8_t face, const Vec3& click, uintptr_t player)
{
    bool ok = false;
    __try
    {
        ok = mode.useItemOnVt(item, target, face, click, player);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }
    return ok;
}

static bool sehBlockItemUseOn(const ItemInstance& item, const BlockPos& target,
                              uint8_t face, const Vec3& click, uintptr_t player)
{
    bool ok = false;
    __try
    {
        ok = blockItemUseOn(item, target, face, click, player);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ok = false;
    }
    return ok;
}

static bool placeUnder(LocalPlayer& local, GameMode& mode, const ItemInstance& item,
                       uintptr_t player, bool tower, bool platform, int& platformY,
                       bool& platformYSolid, float retryMs, Scaffold::Strategy strategy,
                       unsigned int& lastPlaceTick)
{
    static std::unordered_map<BlockPos, uint32_t, BlockPosHash> tried;
    static uint32_t tickCounter = 0;
    ++tickCounter;

    Vec3 pos = local.pos();
    BlockPos base = blockFromVec(pos);
    Vec3 vel = local.velocity();
    float hSpeed = vecLength2D(vel);

    uint8_t face = 1;
    BlockPos target;

    int tx = base.x;
    int tz = base.z;
    if (hSpeed > 0.05f)
    {
        Vec3 dir = normalize({vel.x, 0.0f, vel.z});
        tx += (int)std::lround(dir.x);
        tz += (int)std::lround(dir.z);
    }

    if (platform && platformYSolid)
        target = {tx, platformY, tz};
    else
        target = {tx, base.y - 2, tz};

    auto it = tried.find(target);
    if (it != tried.end())
    {
        uint32_t elapsed = tickCounter - it->second;
        float useRetry = retryMs;
        if (placestats::fastPlaceMs >= 0 && (float)placestats::fastPlaceMs < useRetry)
            useRetry = (float)placestats::fastPlaceMs;
        uint32_t need = (uint32_t)(useRetry / 50.0f);
        if (elapsed < need)
            return false;
    }

    Vec3 click{0.5f, 1.0f, 0.5f};
    bool ok = false;

    placestats::gmValid = mode.valid();
    placestats::inputValid = MoveInputHandler(local.moveInputAddr()).valid();
    placestats::lastX = target.x;
    placestats::lastY = target.y;
    placestats::lastZ = target.z;

    switch (strategy)
    {
    case Scaffold::Strategy::Auto:
    {
        bool any = false;
        if (mode.valid())
            any = sehUseItemOn(mode, item, target, face, click, player);
        if (sehBlockItemUseOn(item, target, face, click, player))
            any = true;
        ok = any;
        break;
    }
    case Scaffold::Strategy::PhysicalLook:
        ok = false;
        break;
    case Scaffold::Strategy::GameModeWrapper:
        ok = mode.valid() && sehUseItemOn(mode, item, target, face, click, player);
        break;
    case Scaffold::Strategy::UseItemOnVt:
        ok = mode.valid() && sehUseItemOnVt(mode, item, target, face, click, player);
        break;
    case Scaffold::Strategy::BlockItemUseOn:
        ok = sehBlockItemUseOn(item, target, face, click, player);
        break;
    case Scaffold::Strategy::LookDownInput:
    {
        float oldPitch = local.pitch();
        local.setPitch(89.0f);
        MoveInputHandler input(local.moveInputAddr());
        if (input.valid())
            input.clickFast();
        local.setPitch(oldPitch);
        ok = true;
        break;
    }
    }

    tried[target] = tickCounter;
    if (tried.size() > 256)
        tried.clear();

    placestats::attempts++;
    placestats::lastOk = ok;

    (void)tower;
    (void)lastPlaceTick;
    return ok;
}

void Scaffold::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    GameMode mode = game.gameMode();

    ItemInstance item = local.heldItem();

    if (autoSwap_ && (!item.valid() || !item.isBlockItem()))
    {
        Inventory inv = local.inventory();
        if (inv.valid())
        {
            for (int slot = 0; slot < 9; ++slot)
            {
                uintptr_t addr = inv.hotbarItem(slot);
                ItemInstance candidate(addr);
                if (candidate.valid() && candidate.isBlockItem() && candidate.count() > 0)
                {
                    local.setHotbarSlot(slot);
                    item = candidate;
                    break;
                }
            }
        }
    }

    if (!item.valid() || !item.isBlockItem() || item.count() <= 0)
        return;

    if (platform_ && !platformYSolid_)
    {
        platformY_ = blockFromVec(local.pos()).y - 2;
        platformYSolid_ = true;
    }

    if (!tower_ && local.onGround() && !platform_)
    {
        Vec3 vel = local.velocity();
        if (vecLength2D(vel) < 0.01f)
            return;
    }

    if (physPhase_ > 0)
    {
        advancePhys(local);
        return;
    }
    if (physCooldown_ > 0)
        --physCooldown_;

    bool direct = strategy_ == Strategy::Auto || strategy_ == Strategy::GameModeWrapper ||
                  strategy_ == Strategy::UseItemOnVt || strategy_ == Strategy::BlockItemUseOn ||
                  strategy_ == Strategy::LookDownInput;
    if (direct)
    {
        placeUnder(local, mode, item, local.address(), tower_, platform_,
                   platformY_, platformYSolid_, retryMs_, strategy_, lastPlaceTick_);
    }

    bool physical = strategy_ == Strategy::Auto || strategy_ == Strategy::PhysicalLook;
    if (physical && physCooldown_ <= 0 && !ImGui::GetIO().WantCaptureMouse)
        startPhys(local);
}

void Scaffold::drawSettings()
{
    int strategy = (int)strategy_;
    const char* names[] = {"Auto (try all)", "GameMode Wrapper", "UseItemOn vtable",
                           "BlockItem::useOn", "LookDown + Input", "Physical RMB"};
    if (widgets::CCombo("Strategy##sc", &strategy, names, 6))
        strategy_ = (Strategy)strategy;
    ImGui::Checkbox("Tower (place while jumping)##sc", &tower_);
    ImGui::Checkbox("Platform mode##sc", &platform_);
    ImGui::Checkbox("Auto swap to block##sc", &autoSwap_);
    widgets::CSlider("Retry ms##sc", &retryMs_, 50.0f, 1000.0f, "%.0f");
    widgets::CSlider("Bridge angle##sc", &bridgeAngle_, 30.0f, 89.0f, "%.0f");
}

MC_REGISTER_MODULE(Scaffold);

}
