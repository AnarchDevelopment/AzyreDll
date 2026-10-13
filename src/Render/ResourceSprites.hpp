#pragma once

namespace mc::resources {

// Carga (lazy, cacheada) un RCDATA de imagen por su resource id y devuelve el
// ID3D11ShaderResourceView* (o nullptr si fallo). Pensado para sprites sueltos
// (cabezas del PlayerList, etc). Se libera en dx11::shutdown().
void* srv(int resourceId);
void unloadAll();

}
