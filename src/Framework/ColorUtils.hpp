#pragma once

#include <imgui.h>

namespace mc {

// Puerto global del ColorUtils de Solstice (src/Utils/MiscUtils/ColorUtils):
// paletas exactas de Interface.hpp + el ciclo LerpColors/Rainbow. El "index"
// es un desplazamiento de fase en ms; el tema/velocidad/saturacion los
// maneja el modulo Interface a traves de los setters.
namespace colors {

int theme();
void setTheme(int t); // 0 Trans, 1 Rainbow, 2 Bubblegum, 3 Watermelon, 4 Sunset, 5 Poison
float colorSpeed();
void setColorSpeed(float v);
float saturation();
void setSaturation(float v);

// getThemedColor(index): el color del tema fluyendo con el tiempo y
// desplazado por index (ms).
ImU32 getThemedColor(float index, float alpha);

} // namespace

}
