#pragma once

namespace mc::mousehook {

// Hook a MouseDevice::feed del juego: la entrada universal de input que el
// HIDController usa para el wheel (actionButtonId == 4). Con el RVA en
// off::fn::MouseDeviceFeed, captura el scroll sobre la ventana del juego.
bool install();
void uninstall();

}
