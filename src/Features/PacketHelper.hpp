#pragma once

#include "Framework/Math.hpp"

#include <atomic>
#include <cstddef>
#include <vector>

// ============================================================================
// Blink — buffer de paquetes de movimiento (estilo Solstice Blink).
//
// Mientras esta activo, el hook de sendToServer CAPTURA los
// MovePlayerPacket salientes: se copian byte a byte a un buffer propio
// (el original muere con el juego — la copia conserva la vtable y
// serializa normal al reenviar) y NO llegan al server. El resto de los
// paquetes (chat, interaccion, use-item) pasa normal.
//
// El server (y los demas jugadores) te ven congelado en la ultima
// posicion liberada — lastSent() la expone para dibujar la caja. Al
// desactivar, los paquetes se reenvian en rafaga via sendRawPacket:
// el server te ve "teletransportarte" a donde realmente estas.
// ============================================================================

namespace mc::blink {

// Capturar (o actualizar lastSent con) el paquete saliente `p`.
// Retorna true si el paquete fue capturado — el hook NO debe pasarlo
// al original. Llamado desde hkSendToServer (game thread).
bool onOutgoingPacket(uintptr_t p);

bool active();
void activate();
// Drena el buffer reenviando todo con los handles capturados del hook.
void deactivate();

// Ultima posicion que el SERVER conoce (la del ultimo MovePlayerPacket
// que SI llego a salir) — la caja del Blink se dibuja ahi.
Vec3 lastSent();

size_t pendingCount();

} // namespace mc::blink
