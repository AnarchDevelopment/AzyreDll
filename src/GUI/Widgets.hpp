#pragma once

#include <imgui.h>
#include <string>
#include <utility>
#include <vector>

namespace mc::widgets {

float EaseOutExpo(float t);

bool CCombo(const char* label, int* current_item, const std::vector<std::string>& items);
bool CCombo(const char* label, int* current_item, const char* const items[], int count);

// Checkbox: en el estilo Solstice renderiza el toggle de sw::Toggle, en el
// estilo Azyre el ImGui::Checkbox normal.
bool Checkbox(const char* label, bool* v);

// Routing por estilo de los inputs de texto y colores (Solstice: sw::).
bool InputText(const char* label, char* buf, size_t size);
bool ColorEdit3(const char* label, float* col3, ImGuiColorEditFlags flags = 0);
bool ColorEdit4(const char* label, float* col4, ImGuiColorEditFlags flags = 0);

bool CSlider(const char* label, float* v, float v_min, float v_max, const char* format = "%.2f");
bool CSlider(const char* label, int* v, int v_min, int v_max, const char* format = "%d");

// Efectos Fluent reutilizables.
void SoftShadow(ImDrawList* d, const ImVec2& min, const ImVec2& max,
                float rounding, float strength, float spread);
void AcrylicPanel(ImDrawList* d, const ImVec2& min, const ImVec2& max,
                  float rounding, float alpha, float sw, float sh,
                  float shadowStrength = 34.0f, float shadowSpread = 14.0f,
                  bool drawBorder = true);

// Glow negro SOLO en los bordes exteriores de una pila de rects (staircase).
// - Lado izquierdo de cada rect: clippeado verticalmente a la altura de ESE rect
//   (sin sangrado hacia los seams horizontales interiores).
// - Borde superior del primer rect y borde inferior del ultimo rect.
// rects: lista ordenada de (min, max) de arriba a abajo.
void GlowBorder(ImDrawList* d,
                const std::vector<std::pair<ImVec2, ImVec2>>& rects,
                float strength, float spread);

}

