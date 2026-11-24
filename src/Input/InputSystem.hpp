#pragma once

namespace mc::input {

void init();
void shutdown();
void updateFrame();
bool windowActive();

// Reaplica el estado del cursor para la captura de UI actual. Los paneles
// propios lo llaman en el instante de abrir/cerrar para que el cursor del
// sistema quede libre sin esperar al siguiente frame.
void refreshUiCapture();

}
