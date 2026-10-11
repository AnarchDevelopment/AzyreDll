#pragma once

#include "Framework/Math.hpp"
#include "Modules/Module.hpp"

#include <imgui.h>

namespace mc {

class LocalPlayer;
class BlockSource;
class ItemInstance;

class Trajectories : public Module
{
public:
    Trajectories();

    void onRender() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"autoItem", autoItem_},
            {"speed", speed_},
            {"gravity", gravity_},
            {"drag", drag_},
            {"maxTime", maxTime_},
            {"drawLanding", drawLanding_},
            {"thickness", thickness_},
            {"color", {color_.x, color_.y, color_.z, color_.w}},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        autoItem_ = j.value("autoItem", autoItem_);
        speed_ = j.value("speed", speed_);
        gravity_ = j.value("gravity", gravity_);
        drag_ = j.value("drag", drag_);
        maxTime_ = j.value("maxTime", maxTime_);
        drawLanding_ = j.value("drawLanding", drawLanding_);
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
    // Llena pts (polyline del arco) y, si colisiona, landingIsValid + landing.
    void simulate(const LocalPlayer& lp, const BlockSource& bs,
                  std::vector<Vec3>& pts, bool& landingValid, Vec3& landing);

    void itemParams(const ItemInstance& held); // autoItem_: ajusta speed/gravity

    bool autoItem_ = true;
    float speed_ = 1.5f;      // bloques/tick
    float gravity_ = 0.03f;   // bloques/tick^2
    float drag_ = 0.99f;
    float maxTime_ = 3.0f;    // segundos
    bool drawLanding_ = true;
    float thickness_ = 2.0f;
    ImVec4 color_ = ImVec4(0.95f, 0.95f, 1.00f, 0.70f);
    float fov_ = 110.0f;
    bool fovInited_ = false;
};

}