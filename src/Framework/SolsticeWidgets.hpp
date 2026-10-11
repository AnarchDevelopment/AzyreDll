#pragma once

#include <imgui.h>

namespace mc {

// Widgets estilo Solstice (puerto de los BoolSetting/Enum/Number de
// ModernDropdown.cpp). Se usan automaticamente en el estilo Solstice via el
// routing de widgets::CSlider/CCombo/Checkbox. Viven DENTRO de la ventana
// ImGui actual: reservan layout con Dummy() y dibujan con el drawlist de la
// ventana (por eso la ventana ##solsettings se expande sola).
namespace sw {

// Toggle (BoolSetting): fila oscura, nombre a la izquierda, checkmark con
// glow del color del tema a la derecha, revelado con boolScale (dt*10).
bool Toggle(const char* label, bool* v);

// Slider (NumberSetting): fila oscura, nombre + valor, barra con gradiente
// del tema; se arrastra con el click sobre la fila.
bool Slider(const char* label, float* v, float mn, float mx, const char* fmt = "%.2f");
bool SliderInt(const char* label, int* v, int mn, int mx, const char* fmt = "%d");

// Combo (EnumSetting): fila oscura, nombre a la izquierda y el valor actual
// en el color del tema a la derecha; click abre un popup custom.
bool Combo(const char* label, int* current, const char* const items[], int count);

// Campo de texto: fila oscura, nombre a la izquierda, input a la derecha.
bool TextInput(const char* label, char* buf, size_t bufsize);

// Color (ColorSetting): fila oscura con el swatch actual a la derecha;
// click abre el picker en un popup.
bool Color(const char* label, float* col, int components);

} // namespace

}
