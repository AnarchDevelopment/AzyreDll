#pragma once

#include <windows.h>

namespace mc::wheel {

void setModule(void* moduleHandle);
bool install();
void uninstall();
void addWinrtDelta(int wheelDelta);
// Delta capturado desde el hook a MouseDevice::feed del juego (la fuente
// que funciona sobre la ventana activa).
void addGameDelta(int wheelDelta);
float consume();
float lastValue();

}
