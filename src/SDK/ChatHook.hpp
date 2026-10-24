#pragma once

#include <string>

namespace mc::chathook {

bool install();
void uninstall();

// Mensaje visible solo en el chat local del cliente (no se envia al servidor).
bool display(const std::string& text);

// Diagnostico de colocacion: arma una ventana en la que TODO paquete
// saliente queda loggeado con su vtable (RVA) — y el UseItemPacket con
// sus campos exactos + un backtrace que nombra la ruta real de
// colocacion del juego. Lo arma el boton Trace del Scaffold.
void armPacketTrace(unsigned seconds);

// Envia un paquete construido por nosotros via sendToServer (self y
// NetworkIdentifier se capturan de los envios propios del juego en el
// hook). Devuelve false si aun no hay handles capturados o el hook no
// esta instalado. SEH-guardado.
bool sendRawPacket(void* packet);

}
