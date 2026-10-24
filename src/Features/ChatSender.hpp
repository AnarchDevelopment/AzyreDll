#pragma once

#include <cstdint>
#include <string>

// ============================================================================
// Envio de chat al servidor.
//
// No hay ctor conocido de TextPacket en este build: la via segura es CLONAR
// un TextPacket real saliente (capturado en el hook de sendToServer con sus
// strings reales: xuid, platform id, etc.), reescribir SOLO el Message y
// reenviarlo por chathook::sendRawPacket con los handles capturados.
//
// sendToServer NO destruye el paquete (lo confirma el Blink, que reenvia
// copias planas): nuestros clones se libera NOSOTROS tras el envio.
// ============================================================================

namespace mc::chatsend {

// Llamado por ChatHook en cada TextPacket saliente: captura/refresca la
// plantilla (solo type=1 chat, sin comandos ni mensajes vacios).
void onOutgoingTextPacket(uintptr_t packet);

// Envia un mensaje de chat al servidor. false si no hay plantilla aun
// (el jugador debe escribir un chat normal una vez en la partida) o si
// el envio fallo.
bool send(const std::string& text);

bool haveTemplate();

// Obfuscacion antispam (misma señal para hash/dedup del server):
//   0 = None
//   1 = Invisible  — 1..3 zero-width spaces (U+200B) en bordes de char
//   2 = Suffix     — tag aleatorio al final ("_a3f")
//   3 = Full       — case-flip + invisible + suffix
std::string obfuscate(const std::string& text, int mode);

}
