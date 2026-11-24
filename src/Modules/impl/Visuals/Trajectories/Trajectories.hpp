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

    std::string suffix() const override
    {
        return autoItem_ ? "Auto" : "Custom";
    }

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
            {"hitEntities", hitEntities_},
            {"showInfo", showInfo_},
            {"bounce", bounce_},
            {"bowCharge", bowCharge_},
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
        hitEntities_ = j.value("hitEntities", hitEntities_);
        showInfo_ = j.value("showInfo", showInfo_);
        bounce_ = j.value("bounce", bounce_);
        bowCharge_ = j.value("bowCharge", bowCharge_);
        if (j.contains("color") && j["color"].is_array() && j["color"].size() == 4)
        {
            color_.x = j["color"][0].get<float>();
            color_.y = j["color"][1].get<float>();
            color_.z = j["color"][2].get<float>();
            color_.w = j["color"][3].get<float>();
        }
    }

private:
    // Simulacion completa: polyline del arco, impacto (bloque o entidad) y datos
    // de vuelo para el marcador.
    struct SimResult
    {
        std::vector<Vec3> points;
        bool landingValid = false;
        Vec3 landing{};
        bool entityHit = false;
        float distance = 0.0f;
        float flightTime = 0.0f;
    };

    void simulate(const LocalPlayer& lp, const BlockSource& bs, SimResult& out);

    // Devuelve false cuando el item en mano no es un lanzable: sin esto el
    // modulo dibujaba una trayectoria falsa con espadas, bloques de mano, etc.
    bool itemParams(const ItemInstance& held);

    bool autoItem_ = true;
    float speed_ = 1.5f;      // bloques/tick
    float gravity_ = 0.03f;   // bloques/tick^2
    float drag_ = 0.99f;
    float maxTime_ = 3.0f;    // segundos
    bool drawLanding_ = true;
    float thickness_ = 2.0f;
    bool hitEntities_ = true; // corta la curva en el primer jugador alcanzado
    bool showInfo_ = true;    // distancia y tiempo de vuelo junto al marcador
    bool bounce_ = false;     // rebote del proyectil contra bloques
    float bowCharge_ = 1.0f;  // 0..1: carga del arco/crossbow (escala la velocidad)
    ImVec4 color_ = ImVec4(0.95f, 0.95f, 1.00f, 0.70f);
};

}