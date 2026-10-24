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

// Unlock FPS (DXGI): fuerza SyncInterval=0 en Present (y ALLOW_TEARING si
// el swapchain lo soporta) — se salta el vsync/cap del compositor. Lo
// controla el modulo UnlockFPS.
void setUnlockFps(bool v);
bool unlockFps();
void setAllowTearing(bool v);
bool allowTearing();

// Info del swapchain (0/false si aun no esta disponible).
float swapchainRefreshHz();
bool swapchainTearingSupported();

// Product Sans Bold horneado a 26px (widgets HUD: ArrayList Solstice).
// nullptr si el resource no cargo.
ImFont* hudFont();

// Product Sans regular a 16px — la usa el estilo Rise (font oficial de Rise).
// nullptr si el resource no cargo.
ImFont* riseFont();

// Mojangles (fuente Minecraft) a 16px — la usa el PlayerList (TAB).
// nullptr si el resource no cargo.
ImFont* gameFont();

}
