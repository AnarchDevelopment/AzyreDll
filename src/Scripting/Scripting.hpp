#pragma once

#include <string>

namespace mc::scripts {

// Carga todos los .lua de <base>\Azyre\Scripts como modulos.
// Debe llamarse ANTES de config::loadAll() para que sus estados persistidos
// se apliquen.
void loadAll();

// Recompila los scripts ya cargados y agrega los nuevos del directorio.
void reload();

// Cierra las VM de todos los scripts (idempotente).
void shutdown();

int count();
int errorCount();
const char* lastError();

}
