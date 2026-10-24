#pragma once

#include <imgui.h>

namespace mc::rw {

// Rise 6.9.5 faithful value widgets (BooleanValue, NumberValue, ModeValue, StringValue, ColorValue).
// Utilizados automáticamente en el estilo Rise (gui::style() == 2) vía widgets::.
bool Toggle(const char* label, bool* v);

bool Slider(const char* label, float* v, float mn, float mx, const char* fmt = "%.2f");
bool SliderInt(const char* label, int* v, int mn, int mx, const char* fmt = "%d");

bool Combo(const char* label, int* current, const char* const items[], int count);

bool TextInput(const char* label, char* buf, size_t bufsize);

bool Color(const char* label, float* col, int components);

} // namespace mc::rw
