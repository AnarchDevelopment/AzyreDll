#include "PlayerNotifier.hpp"

#include "Features/ECS/ECS.hpp"
#include "Features/Friends.hpp"
#include "Framework/Log.hpp"
#include "GUI/Widgets.hpp"
#include "Modules/ModuleManager.hpp"

#include <windows.h>

#include <cctype>

namespace mc {

PlayerNotifier::PlayerNotifier()
    : Module("PlayerNotifier", "Alerts when players join or leave your render distance",
             Category::Misc, 0)
{
    markHasSettings();
}

void PlayerNotifier::onEnable()
{
    auto& d = ecs::get().dispatcher;
    d.sink<ecs::PlayerJoined>().connect<&PlayerNotifier::onPlayerJoined>(*this);
    d.sink<ecs::PlayerLeft>().connect<&PlayerNotifier::onPlayerLeft>(*this);
    d.sink<ecs::LocalPlayerFound>().connect<&PlayerNotifier::onLocalFound>(*this);
    d.sink<ecs::HurtEvent>().connect<&PlayerNotifier::onHurt>(*this);
}

void PlayerNotifier::onDisable()
{
    auto& d = ecs::get().dispatcher;
    d.sink<ecs::PlayerJoined>().disconnect<&PlayerNotifier::onPlayerJoined>(*this);
    d.sink<ecs::PlayerLeft>().disconnect<&PlayerNotifier::onPlayerLeft>(*this);
    d.sink<ecs::LocalPlayerFound>().disconnect<&PlayerNotifier::onLocalFound>(*this);
    d.sink<ecs::HurtEvent>().disconnect<&PlayerNotifier::onHurt>(*this);
    lastNotify_.clear();
}

bool PlayerNotifier::throttled(const std::string& name)
{
    ULONGLONG now = GetTickCount64();
    auto it = lastNotify_.find(name);
    if (it != lastNotify_.end() && now - it->second < (ULONGLONG)(cooldown_ * 1000.0f))
        return true;
    lastNotify_[name] = now;
    return false;
}

void PlayerNotifier::onPlayerJoined(const ecs::PlayerJoined& ev)
{
    if (!notifyJoin_ || ev.name.empty())
        return;
    // Suppress the initial burst of everyone already in the world when
    // we first spawn (entities are created during the first syncs).
    if (GetTickCount64() < joinGraceUntil_)
        return;
    if (ignoreFriends_ && friends::isFriend(ev.name))
        return;
    if (throttled(ev.name))
        return;
    ModuleManager::get().notifyInfo(ev.name + " joined");
}

void PlayerNotifier::onPlayerLeft(const ecs::PlayerLeft& ev)
{
    if (!notifyLeave_ || ev.name.empty())
        return;
    if (ignoreFriends_ && friends::isFriend(ev.name))
        return;
    if (throttled(ev.name))
        return;
    ModuleManager::get().notifyInfo(ev.name + " left");
}

void PlayerNotifier::onLocalFound(const ecs::LocalPlayerFound&)
{
    // Grace window: right after spawning the ECS creates entities for all
    // players already in range — those are not real "joins".
    joinGraceUntil_ = GetTickCount64() + 3000ull;

    if (notifyDeath_ && waitingRespawn_)
    {
        waitingRespawn_ = false;
        ModuleManager::get().notifyInfo("Respawned");
    }
}

void PlayerNotifier::onHurt(const ecs::HurtEvent&)
{
    if (!underAttack_)
        return;
    ULONGLONG now = GetTickCount64();
    if (now - lastAttackWarn_ < (ULONGLONG)(attackCooldown_ * 1000.0f))
        return;
    lastAttackWarn_ = now;
    ModuleManager::get().notifyInfo("Under attack!");
}

void PlayerNotifier::onTick()
{
    // Death detection: watch the local player's Health component
    // transition from alive to zero.
    auto& ecs = ecs::get();
    if (!ecs.hasLocal())
    {
        lastHealth_ = -1;
        return;
    }

    const auto* h = ecs.registry.try_get<ecs::Health>(ecs.local());
    if (!h)
        return;

    if (notifyDeath_ && lastHealth_ > 0 && h->current <= 0)
    {
        waitingRespawn_ = true;
        ModuleManager::get().notifyInfo("You died");
    }
    lastHealth_ = h->current;
}

void PlayerNotifier::drawSettings()
{
    widgets::Checkbox("Notify join##pn", &notifyJoin_);
    widgets::Checkbox("Notify leave##pn", &notifyLeave_);
    widgets::Checkbox("Notify death##pn", &notifyDeath_);
    widgets::Checkbox("Under attack warning##pn", &underAttack_);
    if (underAttack_)
        widgets::CSlider("Attack cooldown##pn", &attackCooldown_, 1.0f, 15.0f, "%.0f s");
    widgets::Checkbox("Ignore friends##pn", &ignoreFriends_);
    widgets::CSlider("Per-name cooldown##pn", &cooldown_, 0.0f, 15.0f, "%.0f s");
}

nlohmann::json PlayerNotifier::saveSettings() const
{
    return nlohmann::json{
        {"notifyJoin", notifyJoin_},
        {"notifyLeave", notifyLeave_},
        {"notifyDeath", notifyDeath_},
        {"underAttack", underAttack_},
        {"ignoreFriends", ignoreFriends_},
        {"cooldown", cooldown_},
        {"attackCooldown", attackCooldown_},
    };
}

void PlayerNotifier::loadSettings(const nlohmann::json& j)
{
    notifyJoin_ = j.value("notifyJoin", notifyJoin_);
    notifyLeave_ = j.value("notifyLeave", notifyLeave_);
    notifyDeath_ = j.value("notifyDeath", notifyDeath_);
    underAttack_ = j.value("underAttack", underAttack_);
    ignoreFriends_ = j.value("ignoreFriends", ignoreFriends_);
    cooldown_ = j.value("cooldown", cooldown_);
    if (cooldown_ < 0.0f)
        cooldown_ = 0.0f;
    if (cooldown_ > 15.0f)
        cooldown_ = 15.0f;
    attackCooldown_ = j.value("attackCooldown", attackCooldown_);
    if (attackCooldown_ < 1.0f)
        attackCooldown_ = 1.0f;
    if (attackCooldown_ > 15.0f)
        attackCooldown_ = 15.0f;
}

MC_REGISTER_MODULE(PlayerNotifier);

}
