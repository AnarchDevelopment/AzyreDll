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

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"lengthSec", lengthSec_},
            {"thickness", thickness_},
            {"fov", fov_},
            {"color", {color_.x, color_.y, color_.z, color_.w}},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        lengthSec_ = j.value("lengthSec", lengthSec_);
        thickness_ = j.value("thickness", thickness_);
        fov_ = j.value("fov", fov_);
        if (j.contains("color") && j["color"].is_array() && j["color"].size() == 4)
        {
            color_.x = j["color"][0].get<float>();
            color_.y = j["color"][1].get<float>();
            color_.z = j["color"][2].get<float>();
            color_.w = j["color"][3].get<float>();
        }
    }

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
