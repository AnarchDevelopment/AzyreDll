#pragma once

#include <imgui.h>
#include <string>
#include <vector>

namespace mc::widgets {

float EaseOutExpo(float t);

bool CCombo(const char* label, int* current_item, const std::vector<std::string>& items);
bool CCombo(const char* label, int* current_item, const char* const items[], int count);

bool CSlider(const char* label, float* v, float v_min, float v_max, const char* format = "%.2f");
bool CSlider(const char* label, int* v, int v_min, int v_max, const char* format = "%d");

// Efectos Fluent reutilizables.
void SoftShadow(ImDrawList* d, const ImVec2& min, const ImVec2& max,
                float rounding, float strength, float spread);
void AcrylicPanel(ImDrawList* d, const ImVec2& min, const ImVec2& max,
                  float rounding, float alpha, float sw, float sh,
                  float shadowStrength = 34.0f, float shadowSpread = 14.0f);

}
