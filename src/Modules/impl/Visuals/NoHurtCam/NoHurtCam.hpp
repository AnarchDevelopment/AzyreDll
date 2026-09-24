#pragma once

#include "Modules/Module.hpp"
#include "SDK/CodePatch.hpp"

namespace mc {

class NoHurtCam : public Module
{
public:
    NoHurtCam();

    void onEnable() override;
    void onDisable() override;
    void onShutdown() override;
    void drawSettings() override;

private:
    uintptr_t site_ = 0;
    patch::CodePatch patch_;
};

}
