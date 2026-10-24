#pragma once

#include "Modules/Module.hpp"

#include "Features/PacketHelper.hpp"

#include <string>

namespace mc {

// FakeLag — partial variant of Blink: holds outgoing MovePlayerPackets in
// the blink buffer for a short window (time and/or depth limited) and then
// releases them in a FIFO burst, cycling forever.
//
// Reuses the PROVEN blink:: packet pipeline end to end (capture in
// hkSendToServer, POD byte copies, sendRawPacket drain, 600-packet
// self-drain safety cap) — no hooks of its own, no new packet paths, and
// all state transitions are mutex-guarded on the shared buffer. The only
// new logic here is the cycle trigger + an optional "release on damage"
// so the burst lands when the enemy thinks you are still behind the old
// position.
//
// NOTE: shares the blink buffer — refuses to run while the Blink module
// is enabled (two owners would desync each other's cycles).
class FakeLag : public Module
{
public:
    FakeLag();

    void onEnable() override;
    void onDisable() override;
    void onShutdown() override;
    void onTick() override;
    void onHurt(void* actor, void* source, float& damage, bool& cancel) override;
    void drawSettings() override;

    std::string suffix() const override
    {
        size_t n = blink::pendingCount();
        return n ? std::to_string(n) + "p" : "";
    }

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    void releaseCycle(ULONGLONG now);
    bool releaseConditionsMet() const;

    float holdMs_ = 150.0f;     // 0 = depth-only
    int maxPackets_ = 6;        // 0 = time-only (both 0 is clamped to 6)
    bool releaseOnDamage_ = false;

    ULONGLONG cycleStart_ = 0;
};

}
