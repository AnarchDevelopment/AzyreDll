#pragma once

#include "Modules/Module.hpp"

#include <windows.h>

namespace mc {

class Parkour : public Module
{
public:
    Parkour();

    void onEnable() override;
    void onTick() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    float edgeDistance_ = 0.05f;
    float jumpHeight_ = 0.42f;
    float boost_ = 0.20f;
    bool preserveSpeed_ = true;
    bool checkSneak_ = true;
    bool checkFallDepth_ = false;

    ULONGLONG lastJumpMs_ = 0;
};

} // namespace mc
