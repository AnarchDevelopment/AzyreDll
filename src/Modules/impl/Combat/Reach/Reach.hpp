#pragma once

#include "Modules/Module.hpp"

namespace mc {

class Reach : public Module
{
public:
    Reach();

    void onEnable() override;
    void onTick() override;
    void onDisable() override;
    void drawSettings() override;

private:
    void writeReach();

    float reach_ = 6.0f;
    float saved_ = 0.0f;
    bool savedValid_ = false;
};

}
