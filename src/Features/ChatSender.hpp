#pragma once

#include <cstdint>
#include <string>

// ============================================================================
// Envio de chat al servidor — ruta NATIVA del build (REA, 0.15.10.0).
//
// Se arma el TextPacket de chat a mano (vtable 0xE4BDC0, +0x08=2, +0x0C=1,
// +0x10=1 tipo chat, source en +0x18, mensaje en +0x38) y se envia por la misma
// cadena que usa sub_1400DE4A0 (el metodo de envio del ChatScreen):
// client+0xE8 -> connector+0x98 -> PacketSender vslot 2.
// Si esa cadena no expone un PacketSender valido (sesiones remotas), el paquete
// sale por sendToServer: es la via que el chat nativo termina usando.
// No hace falta ninguna "plantilla" capturada.
// ============================================================================

namespace mc::chatsend {

// Llamado por ChatHook en cada TextPacket saliente: refresca el source name
// autoritativo (el que el propio juego pone al chat).
void onOutgoingTextPacket(uintptr_t packet);

// Envia un mensaje de chat al servidor. false si no hay cliente o el envio
// fue rechazado por ambas rutas.
bool send(const std::string& text);

// Sends a slash-prefixed server command through the build's normal chat
// TextPacket path. Commands are kept verbatim by the Spammer (no obfuscation).
bool sendCommand(const std::string& command);

// Compatibilidad: ya no hay plantilla; true cuando hay cliente al que enviar.
bool haveTemplate();

// Obfuscacion antispam (misma señal para hash/dedup del server):
//   0 = None
//   1 = Invisible  — 1..3 zero-width spaces (U+200B) en bordes de char
//   2 = Suffix     — tag aleatorio al final ("_a3f")
//   3 = Full       — case-flip + invisible + suffix
std::string obfuscate(const std::string& text, int mode);

}
