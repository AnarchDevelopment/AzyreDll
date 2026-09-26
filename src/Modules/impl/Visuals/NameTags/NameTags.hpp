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

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"showHealth", showHealth_},
            {"scaleDistance", scaleDistance_},
            {"fontSize", fontSize_},
            {"maxDistance", maxDistance_},
            {"fov", fov_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        showHealth_ = j.value("showHealth", showHealth_);
        scaleDistance_ = j.value("scaleDistance", scaleDistance_);
        fontSize_ = j.value("fontSize", fontSize_);
        maxDistance_ = j.value("maxDistance", maxDistance_);
        fov_ = j.value("fov", fov_);
    }

private:
    bool showHealth_ = true;
    bool scaleDistance_ = true;
    float maxDistance_ = 64.0f;
    float fontSize_ = 14.0f;
    float fov_ = 110.0f;
    bool fovInited_ = false;
};

}
