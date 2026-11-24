#pragma once

#include <cstdint>

// Registro de golpes melee que GameMode::attack efectivamente mando al server.
//
// En esta build el generic.health del jugador remoto NUNCA se refresca (la
// AttributeInstance conserva el 20 que deja el inicializador), y el evento
// ActorEvent(HURT) no llega por la ruta que el SDK puede observar. Por eso la
// unica fuente real de dano es el propio ataque local: GameMode::attack anota
// aca el dano del arma en mano y los HUDs lo consumen por frame.
//
// Vive fuera de los modulos para que cualquier estimador (TargetHUD, Damage
// Indicators, etc.) comparta el mismo registro sin acoplarse a KillAura.
namespace mc::hitfeed {

// Anota un impacto. `damage` es el dano base del arma con la que se golpeo.
void recordHit(uintptr_t targetAddr, float damage);

// Consume los registros posteriores a `afterSequence` que pertenecen a
// `targetAddr`. Devuelve el dano total, informa cuantos impactos coincidieron
// y deja en `processedSequence` la ultima secuencia leida (avanza aunque el
// registro sea de otro actor, para no recontar los ajenos).
float consumeHits(uintptr_t targetAddr, uint64_t afterSequence,
                  uint64_t& processedSequence, unsigned& hitCount);

// Secuencia global actual. Sirve para anclar el cursor al cambiar de objetivo.
uint64_t sequence();

} // namespace mc::hitfeed
