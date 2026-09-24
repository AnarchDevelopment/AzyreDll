#pragma once

#include "Modules/Module.hpp"

#include <imgui.h>

namespace mc {

class Crosshair : public Module
{
public:
    Crosshair();

    void onRender() override;
    void drawSettings() override;

private:
    float size_ = 7.0f;
    float gap_ = 4.0f;
    float thickness_ = 2.0f;
    bool dot_ = false;
    bool outline_ = true;
    ImVec4 color_ = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
};

}
