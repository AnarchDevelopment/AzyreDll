#pragma once

#include "Modules/Module.hpp"

namespace mc {

class Speed : public Module
{
public:
    Speed();

    void onEnable() override;
    void onTick() override;
    void drawSettings() override;

private:
    float speedPerTick_ = 0.35f;
};

}
