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

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"fov", fov_},
            {"thickness", thickness_},
            {"color", {color_.x, color_.y, color_.z, color_.w}},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        fov_ = j.value("fov", fov_);
        thickness_ = j.value("thickness", thickness_);
        if (j.contains("color") && j["color"].is_array() && j["color"].size() == 4)
        {
            color_.x = j["color"][0].get<float>();
            color_.y = j["color"][1].get<float>();
            color_.z = j["color"][2].get<float>();
            color_.w = j["color"][3].get<float>();
        }
    }

private:
    float fov_ = 110.0f;
    bool fovInited_ = false;
    float thickness_ = 1.5f;
    ImVec4 color_ = ImVec4(0.84f, 0.86f, 0.90f, 0.75f);
};

}
