#pragma once

#include "Framework/Math.hpp"
#include "Modules/Module.hpp"

#include <imgui.h>
#include <windows.h>
#include <vector>

namespace mc {

class Trail : public Module
{
public:
    Trail();

    void onEnable() override;
    void onDisable() override;
    void onRender() override;
    void drawSettings() override;

private:
    struct Sample
    {
        Vec3 pos;
        ULONGLONG time;
    };

    std::vector<Sample> hist_;
    ULONGLONG lastPush_ = 0;
    float lengthSec_ = 2.0f;
    float thickness_ = 1.5f;
    float fov_ = 110.0f;
    bool fovInited_ = false;
    ImVec4 color_ = ImVec4(0.84f, 0.86f, 0.90f, 0.85f);
};

}
