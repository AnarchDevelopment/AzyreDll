#pragma once

namespace mc::dx11 {

bool install();
void shutdown();
void setModule(void* moduleHandle);

// Backdrop acrilico: frame del juego con blur (downscale bilinear).
bool backdropWanted();
bool backdropReady();
void* backdropSrv();

}
