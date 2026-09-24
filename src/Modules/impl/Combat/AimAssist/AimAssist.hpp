#pragma once

#include "Modules/Module.hpp"

namespace mc {

class AimAssist : public Module
{
public:
    AimAssist();

    void onFrame(float dt) override;
    void drawSettings() override;

private:
    float range_ = 4.0f;
    float fov_ = 90.0f;
    float speedH_ = 0.35f;
    float speedV_ = 0.25f;
    bool onlyWeapons_ = false;
};

}
