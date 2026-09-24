#pragma once

#include "Modules/Module.hpp"

namespace mc {

class Velocity : public Module
{
public:
    Velocity();

    void onEnable() override;
    void onDisable() override;
    void onShutdown() override;
    void drawSettings() override;

private:
    int chance_ = 100;
    int hPercent_ = 100;
    int vPercent_ = 100;
};

}
