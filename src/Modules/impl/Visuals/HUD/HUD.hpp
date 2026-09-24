#pragma once

#include "Modules/Module.hpp"

namespace mc {

class HUD : public Module
{
public:
    HUD();

    void onRender() override;
    void drawSettings() override;

private:
    bool stats_ = true;
    bool speed_ = true;
    bool direction_ = true;
};

}
