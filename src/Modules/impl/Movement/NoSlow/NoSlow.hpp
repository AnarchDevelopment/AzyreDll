#pragma once

#include "Modules/Module.hpp"

namespace mc {

class NoSlow : public Module
{
public:
    NoSlow();

    void onTick() override;
    void drawSettings() override;

private:
    float restoreSpeed_ = 0.216f;
    bool onlyGround_ = true;
};

}
