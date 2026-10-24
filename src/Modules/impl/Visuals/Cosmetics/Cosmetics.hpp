#pragma once

#include "Framework/Math.hpp"
#include "Modules/Module.hpp"

#include <imgui.h>
#include <string>

namespace mc {

struct CameraView;

class Cosmetics : public Module
{
public:
    Cosmetics();

    void onRender() override;
    void drawSettings() override;

    std::string suffix() const override
    {
        if (!wings_)
            return halo_ ? "Halo" : "";
        static const char* kNames[] = {"Angel", "Demon", "Butterfly"};
        return kNames[wingStyle_];
    }

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"target", target_},
            {"wings", wings_},
            {"halo", halo_},
            {"wingStyle", wingStyle_},
            {"size", size_},
            {"flapSpeed", flapSpeed_},
            {"opacity", opacity_},
            {"smooth", smooth_},
            {"fovInited", fovInited_},
            {"fov", fov_},
            {"color", {color_.x, color_.y, color_.z, color_.w}},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        target_ = j.value("target", target_);
        if (target_ < 0 || target_ > 2) target_ = 2;
        wings_ = j.value("wings", wings_);
        halo_ = j.value("halo", halo_);
        wingStyle_ = j.value("wingStyle", wingStyle_);
        if (wingStyle_ < 0 || wingStyle_ > 2) wingStyle_ = 0;
        size_ = clampf(j.value("size", size_), 0.4f, 2.0f);
        flapSpeed_ = clampf(j.value("flapSpeed", flapSpeed_), 0.3f, 4.0f);
        opacity_ = clampf(j.value("opacity", opacity_), 0.10f, 1.00f);
        smooth_ = j.value("smooth", smooth_);
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
    struct Pose
    {
        Vec3 feet;
        float yaw = 0.0f;
        float height = 1.8f;
    };

    void drawActor(ImDrawList* d, const Pose& pose, const CameraView& cam,
                   float sw, float sh, float t);

    int target_ = 2; // 0 = Self, 1 = Others, 2 = Both
    bool wings_ = true;
    bool halo_ = true;
    int wingStyle_ = 0;
    float size_ = 1.0f;
    float flapSpeed_ = 1.2f;
    float opacity_ = 0.85f;
    bool smooth_ = true;
    float fov_ = 110.0f;
    bool fovInited_ = false;
    ImVec4 color_ = ImVec4(0.90f, 0.93f, 1.00f, 1.00f);
};

}
