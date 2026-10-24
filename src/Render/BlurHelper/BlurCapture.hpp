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
//
// useSwapChain=false (pantallas del juego abiertas: el frame llega por el
// path de composicion XAML): saltar la fuente 1 por completo — cero
// GetBuffer/CopyResource sobre el backbuffer durante esa transicion. Las
// fuentes 2..4 solo LEEN texturas propias del juego y mantienen el blur
// vivo para los widgets.
ID3D11ShaderResourceView* find(ID3D11RenderTargetView* boundRtv, bool* outFlip,
                               bool useSwapChain);

// Libera la fuente devuelta por find(): las persistentes (clon) no se liberan.
void releaseSource(ID3D11ShaderResourceView* s);

// SRV de la captura RAW (clon full-res, persistente; nullptr si nunca hubo).
ID3D11ShaderResourceView* persistentSrv();

void release();

}
