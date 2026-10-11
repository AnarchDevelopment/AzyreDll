#pragma once

#include <string>
#include <vector>

namespace mc::irc {

// ============================================================================
// IRC — cliente global (Networking/IRC/). Conexion a Libera (#aeglegeneral)
// en un hilo propio con WinSock. El modulo IRC y el comando .irc son solo
// consumers de esta API.
//
// Uso:
//   start(nick)          — conecta, registra y entra al canal (asincrono)
//   stop()               — QUIT + cierre limpio del hilo
//   send(text)           — encola un PRIVMSG al canal (false si no conectado)
//   pollLog()/pollChat() — drena las lineas para mostrar (thread-safe)
//   status()             — Connecting / Connected / Disconnected
// ============================================================================

enum class Status
{
    Offline,
    Connecting,
    Connected,
};

void start(const std::string& nick);
void stop();
bool send(const std::string& text);

// Lineas de progreso de la conexion ("Connecting to Libera...", etc).
std::vector<std::string> pollLog();
// Mensajes del canal ya formateados ("nick: text").
std::vector<std::string> pollChat();

Status status();

}
