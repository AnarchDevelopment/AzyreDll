#pragma once

#include "Features/ECS/Events.hpp"
#include "Modules/Module.hpp"

#include <set>
#include <string>

namespace mc {

// StaffDetector — priority alerts when a configured staff name joins or
// leaves the render distance. Uses the same EnTT pipeline as
// PlayerNotifier (PlayerJoined/PlayerLeft dispatcher events) plus a live
// registry view to show who is currently in range.
class StaffDetector : public Module
{
public:
    StaffDetector();

    void onEnable() override;
    void onDisable() override;
    void drawSettings() override;

    std::string suffix() const override
    {
        return std::to_string(staff_.size());
    }

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

    // Dispatcher sinks
    void onPlayerJoined(const ecs::PlayerJoined& ev);
    void onPlayerLeft(const ecs::PlayerLeft& ev);

    // Consulta publica (la usa el Radar para pintar el staff en rojo).
    bool isStaff(const std::string& name) const;

private:
    void alert(const std::string& name, bool joined);

    std::set<std::string> staff_;
    bool alertJoin_ = true;
    bool alertLeave_ = false;
    bool chatAlert_ = true;  // also print to the in-game chat
    bool soundAlert_ = true;
    char input_[32] = {};
};

}
