#pragma once

#include "Modules/Module.hpp"

namespace mc {

class TargetHUD : public Module
{
public:
    TargetHUD();

    void onRender() override;
    void drawSettings() override;

private:
    float range_ = 8.0f;
    float fov_ = 30.0f;
    bool showHealth_ = true;
};

}
