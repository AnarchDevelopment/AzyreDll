#pragma once

#include "Modules/Module.hpp"

namespace mc {

class NoFall : public Module
{
public:
    NoFall();

    void onTick() override;
    void drawSettings() override;

private:
    bool ghostGround_ = true;
    float ghostThreshold_ = -1.8f;
};

}
