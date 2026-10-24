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

    std::string suffix() const override
    {
        return style_ == 0 ? "2D" : "3D";
    }

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"style", style_},
            {"box", box_},
            {"name", name_},
            {"distance", distance_},
            {"filled", filled_},
            {"gradient", gradient_},
            {"fillAlpha", fillAlpha_},
            {"shadow", shadow_},
            {"shadowStrength", shadowStrength_},
            {"fov", fov_},
            {"color", {color_.x, color_.y, color_.z, color_.w}},
            {"smooth", smooth_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        style_ = j.value("style", style_);
        box_ = j.value("box", box_);
        name_ = j.value("name", name_);
        distance_ = j.value("distance", distance_);
        filled_ = j.value("filled", filled_);
        gradient_ = j.value("gradient", gradient_);
        fillAlpha_ = j.value("fillAlpha", fillAlpha_);
        shadow_ = j.value("shadow", shadow_);
        shadowStrength_ = j.value("shadowStrength", shadowStrength_);
        fov_ = j.value("fov", fov_);
        if (j.contains("color") && j["color"].is_array() && j["color"].size() == 4)
        {
            color_.x = j["color"][0].get<float>();
            color_.y = j["color"][1].get<float>();
            color_.z = j["color"][2].get<float>();
            color_.w = j["color"][3].get<float>();
        }
        smooth_ = j.value("smooth", smooth_);
    }

private:
    int style_ = 0;
    bool box_ = true;
    bool name_ = true;
    bool distance_ = true;
    bool filled_ = false;
    bool gradient_ = false;
    float fillAlpha_ = 0.35f;
    bool shadow_ = true;
    float shadowStrength_ = 32.0f;
    bool smooth_ = true;
    float fov_ = 110.0f;
    bool fovInited_ = false;
    ImVec4 color_ = ImVec4(0.78f, 0.82f, 0.90f, 1.0f);
};

}
