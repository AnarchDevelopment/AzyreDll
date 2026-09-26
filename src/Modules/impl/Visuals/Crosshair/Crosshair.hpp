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

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"size", size_},
            {"gap", gap_},
            {"thickness", thickness_},
            {"dot", dot_},
            {"outline", outline_},
            {"color", {color_.x, color_.y, color_.z, color_.w}},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        size_ = j.value("size", size_);
        gap_ = j.value("gap", gap_);
        thickness_ = j.value("thickness", thickness_);
        dot_ = j.value("dot", dot_);
        outline_ = j.value("outline", outline_);
        if (j.contains("color") && j["color"].is_array() && j["color"].size() == 4)
        {
            color_.x = j["color"][0].get<float>();
            color_.y = j["color"][1].get<float>();
            color_.z = j["color"][2].get<float>();
            color_.w = j["color"][3].get<float>();
        }
    }

private:
    float size_ = 7.0f;
    float gap_ = 4.0f;
    float thickness_ = 2.0f;
    bool dot_ = false;
    bool outline_ = true;
    ImVec4 color_ = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
};

}
