#pragma once

namespace mc::keygate {

// Neutraliza el teclado del juego durante la captura de texto; no modifica
// mensajes ni estado del mouse.
bool install();
void uninstall();

// Armar al abrir el panel; al desarmar queda una gracia corta para que el
// KEYDOWN de salida no despierte la pausa del juego.
void setArmed(bool armed);
// Bloquea T para Bedrock únicamente mientras hay LocalPlayer. La DLL detecta
// la misma tecla por su estado asíncrono y abre CustomChat.
void setTBlockArmed(bool armed);
bool shouldBlockT();
bool shouldCaptureKeyboard();

bool installed();

}
