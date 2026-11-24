#pragma once

namespace mc::menu {

void handleInput();
void render();
bool visible();
float animationAlpha();

// Rect de la ventana del menu (pixeles de pantalla). Actualizado por render().
void windowRect(ImVec2& outMin, ImVec2& outMax);
void setWindowRect(const ImVec2& min, const ImVec2& max);
// Radio de esquina actual del panel glass (lo usa el composite Glass Fluent
// del BlurHelper para que el frost redondeado coincida con el borde ImGui).
float glassRadius();
// Body del estilo Azyre (lo llama gui::sAzyre; el dispatch vive en render()).
void renderAzyre(float t);
// HUD Editor mode (boton center-bottom, no es un modulo)
void renderHudChrome();
bool hudEditing();
void setHudEditing(bool v);

// Captura de input compartida: cualquier panel propio (chat, etc.) la
// enciende mientras esta abierto. uiOwnsInput() es el OR de todas las
// fuentes y es lo que consultan el gate de raw input, el cursor y las
// teclas de texto. Nunca reemplaza a visible()/hudEditing(): se suma.
void setUiCapture(bool on);
bool uiCapture();
bool uiOwnsInput();
}
