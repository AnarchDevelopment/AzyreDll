#pragma once

#include "Modules/Module.hpp"

#include <imgui.h>

namespace mc {

class NameTags : public Module
{
public:
    NameTags();

    void onRender() override;
    void drawSettings() override;

private:
    bool showHealth_ = true;
    bool scaleDistance_ = true;
    float maxDistance_ = 64.0f;
    float fontSize_ = 14.0f;
    float fov_ = 110.0f;
    bool fovInited_ = false;
};

}
