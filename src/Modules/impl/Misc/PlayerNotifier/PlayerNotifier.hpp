#pragma once

#include "Features/ECS/Events.hpp"
#include "Modules/Module.hpp"

#include <map>
#include <string>

namespace mc {

// PlayerNotifier — join/leave/render-distance alerts built on the EnTT
// event pipeline: the ECS registry sinks translate entity construction and
// destruction into PlayerJoined/PlayerLeft dispatcher events, and this
// module just subscribes to what it cares about. No manual list diffing.
class PlayerNotifier : public Module
{
public:
    PlayerNotifier();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

    // Dispatcher sinks
    void onPlayerJoined(const ecs::PlayerJoined& ev);
    void onPlayerLeft(const ecs::PlayerLeft& ev);
    void onLocalFound(const ecs::LocalPlayerFound& ev);
    void onHurt(const ecs::HurtEvent& ev);

private:
    bool throttled(const std::string& name);

    bool notifyJoin_ = true;
    bool notifyLeave_ = true;
    bool notifyDeath_ = true;
    bool underAttack_ = false;
    bool ignoreFriends_ = true;
    float cooldown_ = 3.0f;        // per-name spam protection (seconds)
    float attackCooldown_ = 5.0f;  // "under attack" warning cooldown

    int lastHealth_ = -1;
    bool waitingRespawn_ = false;
    ULONGLONG joinGraceUntil_ = 0; // suppresses the burst of everyone
                                   // already present when you spawn
    ULONGLONG lastAttackWarn_ = 0;
    std::map<std::string, ULONGLONG> lastNotify_;
};

}
