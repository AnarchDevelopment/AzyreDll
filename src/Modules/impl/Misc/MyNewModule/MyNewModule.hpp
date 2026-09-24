#pragma once

#include "Modules/Module.hpp"

namespace mc {

class MyNewModule : public Module
{
public:
    MyNewModule();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void onRender() override;
    void drawSettings() override;

private:
    float exampleValue_ = 1.0f;
    bool exampleFlag_ = false;
};

}
