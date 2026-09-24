#pragma once

#include "Modules/Module.hpp"

namespace mc {

class AntiAFK : public Module
{
public:
    AntiAFK();

    void onEnable() override;
    void onTick() override;
    void drawSettings() override;

private:
    float intervalSec_ = 12.0f;
    bool rotate_ = true;
    bool jump_ = true;
    float angle_ = 25.0f;
    unsigned long long lastAction_ = 0;
};

}
