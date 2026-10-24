#pragma once

#include "Framework/Math.hpp"
#include "Modules/Module.hpp"

#include <imgui.h>
#include <cstdint>
#include <string>
#include <vector>

namespace mc {

class AmbientParticles : public Module
{
public:
    AmbientParticles();

    void onEnable() override;
    void onRender() override;
    void drawSettings() override;

    std::string suffix() const override
    {
        static const char* kModes[] = {"Snow", "Ember", "Petal", "Spark"};
        return kModes[mode_];
    }

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"mode", mode_},
            {"count", count_},
            {"radius", radius_},
            {"speed", speed_},
            {"size", size_},
            {"opacity", opacity_},
            {"customColor", customColor_},
            {"fovInited", fovInited_},
            {"fov", fov_},
            {"color", {color_.x, color_.y, color_.z, color_.w}},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        mode_ = j.value("mode", mode_);
        if (mode_ < 0 || mode_ > 3) mode_ = 0;
        count_ = j.value("count", count_);
        if (count_ < 20) count_ = 20;
        if (count_ > 500) count_ = 500;
        radius_ = clampf(j.value("radius", radius_), 6.0f, 40.0f);
        speed_ = clampf(j.value("speed", speed_), 0.2f, 3.0f);
        size_ = clampf(j.value("size", size_), 0.5f, 4.0f);
        opacity_ = clampf(j.value("opacity", opacity_), 0.05f, 1.00f);
        customColor_ = j.value("customColor", customColor_);
        fov_ = clampf(j.value("fov", fov_), 30.0f, 130.0f);
        fovInited_ = j.value("fovInited", fovInited_);
        if (j.contains("color") && j["color"].is_array() && j["color"].size() == 4)
        {
            color_.x = j["color"][0].get<float>();
            color_.y = j["color"][1].get<float>();
            color_.z = j["color"][2].get<float>();
            color_.w = j["color"][3].get<float>();
        }
    }

private:
    struct Part
    {
        Vec3 off; // offset relativo a los pies del jugador local
        float seed = 0.0f;
        float phase = 0.0f;
    };

    void spawn(Part& p);
    float nextRand();

    std::vector<Part> parts_;
    uint32_t rng_ = 0x9E3779B9u;

    int mode_ = 0;
    int count_ = 160;
    float radius_ = 14.0f;
    float speed_ = 1.0f;
    float size_ = 1.6f;
    float opacity_ = 0.75f;
    bool customColor_ = false;
    float fov_ = 110.0f;
    bool fovInited_ = false;
    ImVec4 color_ = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
};

}
