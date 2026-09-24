#pragma once

#include <string>
#include <vector>

namespace mc::widgets {

float EaseOutExpo(float t);

bool CCombo(const char* label, int* current_item, const std::vector<std::string>& items);
bool CCombo(const char* label, int* current_item, const char* const items[], int count);

bool CSlider(const char* label, float* v, float v_min, float v_max, const char* format = "%.2f");
bool CSlider(const char* label, int* v, int v_min, int v_max, const char* format = "%d");

}
