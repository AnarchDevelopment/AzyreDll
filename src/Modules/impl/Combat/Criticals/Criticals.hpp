#pragma once

#include "Modules/Module.hpp"

namespace mc {

class Criticals : public Module
{
public:
    Criticals();

    void onTick() override;
    void onDisable() override;
    void drawSettings() override;

private:
    bool onlyWhileAttacking_ = true;
    int phase_ = 0;
    bool wasAttacking_ = false;
};

}
