#pragma once

namespace mc::chatopen {

enum class Request
{
    None = 0,
    Chat = 1,
    Command = 2,
};

// Arma/desarma el reemplazo. Cuando está armado, las funciones de apertura
// nativas se suprimen y dejan una solicitud para el panel ImGui; desarmado,
// ambas funciones pasan a través de su implementación original.
void setArmed(bool armed);
bool armed();

// Toma la peticion pendiente (la borra). Hilo de render.
Request consume();

bool install();
void uninstall();

}
