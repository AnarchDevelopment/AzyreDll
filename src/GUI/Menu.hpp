#pragma once

namespace mc::menu {

void handleInput();
void render();
bool visible();
float animationAlpha();

// Rect de la ventana del menu (pixeles de pantalla). Actualizado por render().
void windowRect(ImVec2& outMin, ImVec2& outMax);
// Radio de esquina actual del panel glass (lo usa el composite Glass Fluent
// del BlurHelper para que el frost redondeado coincida con el borde ImGui).
float glassRadius();
// Body del estilo Azyre (lo llama gui::sAzyre; el dispatch vive en render()).
void renderAzyre(float t);
// HUD Editor mode (boton center-bottom, no es un modulo)
void renderHudChrome();
bool hudEditing();
void setHudEditing(bool v);
}
