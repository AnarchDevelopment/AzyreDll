#pragma once

#include "Modules/Module.hpp"

namespace mc {

class Radar : public Module
{
public:
    Radar();

    void onRender() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    int shape_ = 0;        // 0 = circulo, 1 = cuadrado
    float range_ = 64.0f;  // radio en bloques
    float size_ = 140.0f;  // tamano en px
    bool rotate_ = true;   // gira con el yaw del jugador
    bool names_ = true;    // etiquetas con el nombre
    bool blurBg_ = true;   // blur de fondo
};

}
