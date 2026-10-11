#pragma once

#include <imgui.h>

namespace mc {

// Puerto global de los helpers de ImRenderUtils (Solstice-Source) que usa
// el ClickGui y puede usar cualquier widget del proyecto.
namespace renderutils {

float getTextWidth(const char* text, float size);
float getTextHeight(float size);

// fillRectangle de Solstice: solido con alpha multiplicada.
void fillRectangle(const ImVec4& r, ImU32 col, float alpha, float rounding = 0.0f,
                   ImDrawFlags flags = 0, ImDrawList* d = nullptr);

// fillRoundedGradientRectangle de Solstice: dos AddRectFilled + doble
// ShadeVertsLinearColorGradientKeepAlpha (gradiente diagonal entre los dos
// colores, alpha independiente por color).
void fillRoundedGradientRectangle(const ImVec4& r, ImU32 c1, ImU32 c2, float radius,
                                  float a1, float a2, ImDrawFlags flags = 0,
                                  ImDrawList* d = nullptr);

// drawText de Solstice: sombra negra a +1px y texto principal.
void drawText(const ImVec2& pos, const char* text, ImU32 col, float size,
              float alpha, bool shadow = true, ImDrawList* d = nullptr,
              ImFont* font = nullptr);

bool isMouseOver(const ImVec4& r);

} // namespace

}
