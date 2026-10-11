#pragma once

#include "BlurDetail.hpp"

namespace mc::blur::rtwatch {

// Hook de ID3D11DeviceContext::OMSetRenderTargets (vtable slot 33):
// registra el ring de RTs del juego y sustituye el backbuffer por el render
// proxy cuando esta armado.
bool install();

void setRecording(bool on);
void clearRing();

// Render proxy (experimental, opt-in via settings.json "blurProxy": "on").
void armProxy(bool on);
bool enabled();
ID3D11RenderTargetView* proxyRtv();
ID3D11ShaderResourceView* proxyFrameSrv(); // AddRef-eado si el proxy capturo frame
void identifyGameBbRTV(ID3D11RenderTargetView* rtv);

// Captura desde el ring: recorre del mas reciente hacia atras y devuelve un
// SRV del primer RT screen-sized legible (ref nueva; el caller libera).
bool ringCapture(ID3D11ShaderResourceView** outSrv);

void release();

}
