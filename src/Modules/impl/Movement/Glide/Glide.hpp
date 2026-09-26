#pragma once

#include "Modules/Module.hpp"
#include "SDK/CodePatch.hpp"

namespace mc {

class Glide : public Module
{
public:
    Glide();

    void onEnable() override;
    void onDisable() override;
    void onShutdown() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"speed", speed_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        speed_ = j.value("speed", speed_);
    }

private:
    uintptr_t site_ = 0;
    int caveSpeedOffset_ = -1;
    float speed_ = -0.1f;
    patch::CodePatch patch_;
};

}
