#pragma once

#include "Modules/Module.hpp"

namespace mc {

class InventoryWalk : public Module
{
public:
    InventoryWalk();

    void onTick() override;
    void drawSettings() override;

private:
    float speedMult_ = 1.0f;
    bool jumpWithSpace_ = true;
};

}
