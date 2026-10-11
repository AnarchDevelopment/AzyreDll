#pragma once

#include "BlurDetail.hpp"

namespace mc::blur::capture {

// Cadena de fuentes, en orden de preferencia:
//   0. render proxy (si el juego renderizo en el este frame)
//   1. SRV directo del backbuffer + clon CopyResource (la via probada)
//   2. ring de RTs del juego (via rtwatch)
//   3. RT bindeado al Present
//   4. SRVs residuales del pixel shader
// Devuelve un SRV a liberar con releaseSource() (o nullptr).
ID3D11ShaderResourceView* find(ID3D11RenderTargetView* boundRtv, bool* outFlip);

// Libera la fuente devuelta por find(): las persistentes (clon) no se liberan.
void releaseSource(ID3D11ShaderResourceView* s);

// SRV de la captura RAW (clon full-res, persistente; nullptr si nunca hubo).
ID3D11ShaderResourceView* persistentSrv();

void release();

}
