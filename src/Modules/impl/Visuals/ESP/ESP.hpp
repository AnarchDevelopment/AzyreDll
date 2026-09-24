#pragma once

#include "Modules/Module.hpp"

#include <imgui.h>

namespace mc {

class ESP : public Module
{
public:
    ESP();

    void onRender() override;
    void drawSettings() override;

private:
    int style_ = 0;
    bool box_ = true;
    bool name_ = true;
    bool distance_ = true;
    float fov_ = 110.0f;
    bool fovInited_ = false;
    ImVec4 color_ = ImVec4(0.78f, 0.82f, 0.90f, 1.0f);
};

}
