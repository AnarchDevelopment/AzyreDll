#pragma once

struct ImFont;

namespace mc::dx11 {

bool install();
void shutdown();
void setModule(void* moduleHandle);
void* moduleHandle();

// Backdrop acrilico: ver Render/BlurHelper/.
bool backdropWanted();
bool backdropReady();
void* backdropSrv();

// Captura RAW sin blur (diagnostico: F9 en el menu la muestra fullscreen).
void* backdropCaptureSrv();

// Device/context de D3D11 para crear texturas propias (sprites, etc).
void* d3dDevice();
void* d3dContext();

// Product Sans Bold horneado a 26px (widgets HUD: ArrayList Solstice).
// nullptr si el resource no cargo.
ImFont* hudFont();

// Mojangles (fuente Minecraft) a 16px — la usa el PlayerList (TAB).
// nullptr si el resource no cargo.
ImFont* gameFont();

}
