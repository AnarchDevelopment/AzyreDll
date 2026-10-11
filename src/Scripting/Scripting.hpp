#pragma once

#include <string>
#include <vector>

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

struct ScriptInfo
{
    std::string name;
    std::string file;
    bool errored = false;
};

std::vector<ScriptInfo> list();

// Descarga el modulo y borra el .lua del disco. Acepta el nombre del modulo
// o el nombre del archivo.
bool remove(const std::string& name);

}
