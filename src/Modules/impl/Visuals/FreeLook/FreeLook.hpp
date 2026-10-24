#pragma once

#include "Modules/Module.hpp"

namespace mc {

class FreeLook : public Module
{
public:
    FreeLook();

    void onEnable() override;
    void onDisable() override;
    void onFrame(float dt) override;
    void onShutdown() override;

private:
    void clearRotation(bool restoreCamera);

    uintptr_t actor_ = 0;
    float savedYaw_ = 0.0f;
    float savedPitch_ = 0.0f;
    bool captured_ = false;
};

}
