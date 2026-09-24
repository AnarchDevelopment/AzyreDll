#pragma once

#include "Modules/Module.hpp"

#include <imgui.h>

namespace mc {

class Tracers : public Module
{
public:
    Tracers();

    void onRender() override;
    void drawSettings() override;

private:
    float fov_ = 110.0f;
    bool fovInited_ = false;
    float thickness_ = 1.5f;
    ImVec4 color_ = ImVec4(0.84f, 0.86f, 0.90f, 0.75f);
};

}
