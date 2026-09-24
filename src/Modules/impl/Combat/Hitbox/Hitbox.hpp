#pragma once

#include "Modules/Module.hpp"
#include "SDK/CodePatch.hpp"

namespace mc {

class Hitbox : public Module
{
public:
    Hitbox();

    void onEnable() override;
    void onDisable() override;
    void onShutdown() override;
    void drawSettings() override;

private:
    float value_ = 0.6f;
    patch::CodePatch patch_;
};

}
