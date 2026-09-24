#pragma once

#include "Modules/Module.hpp"

namespace mc {

class HighJump : public Module
{
public:
    HighJump();

    void onTick() override;
    void drawSettings() override;

private:
    float boost_ = 0.60f;
};

}
