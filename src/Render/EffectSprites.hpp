#pragma once

namespace mc::fxsprites {

// SRV del sprite del efecto (1..30), nullptr si el id no es valido o la
// carga fallo. Carga lazy con cache por id.
void* srv(int effectId);

// Dimensiones originales del sprite cargado.
bool dims(int effectId, int& w, int& h);

// Libera los SRVs (antes de que dx11 suelte el device).
void unloadAll();

}
