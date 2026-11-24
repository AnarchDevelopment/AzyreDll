#pragma once

#include "Framework/Math.hpp"

#include <cstdint>
#include <unordered_map>

namespace mc::lgh {

// Historial de posiciones por actor, en QPC (como todo el smoothing).
//
// Lo usa Backtrack para reproducir la trayectoria del enemigo con retraso: el
// modulo escribe al actor en la posicion que tenia hace `delay` ms, asi el
// target se mueve igual que en el server pero atrasado (backtrack clasico de
// lag compensation). Escribir/consultar: solo render thread.

// Registra una muestra de la posicion REAL (la del ultimo paquete del server).
// Ignora muestras no finitas o repetidas dentro de kSampleGapMs.
void record(uintptr_t actor, const Vec3& pos);

// Posicion del actor hace `delayMs`. false si no hay ninguna muestra.
// Si el historial cubre menos que `delayMs`, devuelve la muestra mas vieja
// (el retraso arranca progresivamente en vez de saltar).
bool at(uintptr_t actor, double delayMs, Vec3& out);

// Edad de la muestra mas antigua (ms). 0 si no hay historial.
double oldestAgeMs(uintptr_t actor);

void clear(uintptr_t actor);
void clear();

} // namespace mc::lgh
