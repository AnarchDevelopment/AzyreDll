#pragma once

#include <d3d11.h>
#include <dxgi.h>

// ============================================================================
// BlurHelper: backdrop acrilico del menu.
//
// Pipeline: captura del frame (clon CopyResource / ring de RTs / proxy) ->
// cadena progresiva de downscale (1/2, 1/4, 1/8, 1/16) -> gaussiano separable
// de doble iteracion compilado en runtime. Composicion: frost a pantalla
// completa + ventana escarchada con UVs screen-space (Menu.cpp).
//
// Estructura interna (Render/BlurHelper/):
//   BlurHelper.cpp   — orquestacion del pase + politica wanted() + SEH
//   BlurTextures.*   — pool de texturas y formatos
//   BlurShaders.*    — HLSL + compilacion runtime
//   BlurPasses.*     — copyPass/gaussPass + save/restore de estado D3D
//   BlurCapture.*    — cadena de fuentes de captura
//   BlurRTWatch.*    — hook de OMSetRenderTargets (ring + render proxy)
// ============================================================================

namespace mc::blur {

// ---- ciclo de vida (llama dx11 desde el Present hook) ----
void init(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swapChain);
void setDisplaySize(float w, float h);
void onResize();
void shutdown();

// ---- API para Menu / Widgets ----
bool wanted();        // menu visible, toasts recientes o modulos con panel acrilico
bool ready();         // pool de texturas valido
void* srv();          // textura blureada (nullptr = sin contenido: usar dim)
void* captureSrv();   // captura RAW sin blur (debug F9)

// ---- integracion con el Present hook (dx11) ----
// allowBackbufferWrites=false (pantalla del juego abierta: el frame se
// presenta por el path XAML): la cadena de captura+blur SIGUE corriendo
// (los widgets acrilicos mantienen su blur en vivo) pero sin tocar el
// swap chain — sin GetBuffer/CopyResource del backbuffer y sin los
// composites Glass/Mica escritos al backbuffer.
void runPass(ID3D11RenderTargetView* boundRtv, bool allowBackbufferWrites = true);
void identifyGameBbRTV(ID3D11RenderTargetView* rtv);
void setRecording(bool on);
void armProxy(bool on);
void clearRing();
void* takeProxyFrame(); // proxy SRV si el juego renderizo en el este frame

// ---- Widgets acrilicos (HUD) ----
// Los paneles de los widgets (ArrayList, HUD, Watermark, PotionHUD, NameTags,
// Keystrokes, Spotify...) compiten el mismo acabado Glass que el menu. Se
// registran por frame: beginAcrylic() se llama al empezar el render del HUD
// (present hook) y addAcrylicRect() desde cada panel. El BlurHelper compone
// esos rects en el backbuffer con el shader Glass (SDF redondeado + borde +
// refraccion), incluso con el menu cerrado durante el gameplay.
void beginAcrylic();
void addAcrylicRect(float x1, float y1, float x2, float y2, float radius, float alpha);
// true si se registraron paneles acrilicos este frame (permite al Present
// hook correr el pase a full rate en vez del throttle de ~30Hz).
bool hasAcrylic();

// Modo Solstice: desactiva el composite Glass nativo del menu (lo hace D2D).
void setMenuGlass(bool on);

// Debug F11: el composite Mica usa la captura RAW (sin blur) en lugar del
// gaussB — separa "composite dibuja" vs "cadena de blur rota".
void setDebugCompositeRaw(bool on);

// ---- MotionBlur (lógica arst-motion-blur: acumulación full-res EMA) ----
// El módulo aplica acumulación exponencial sobre el backbuffer a resolución
// completa ANTES de que se dibuje el HUD o ImGui, garantizando nitidez total
// y evitando cualquier interferencia con los widgets del juego o del cliente.
void setMotionBlurEnabled(bool on);
bool motionBlurEnabled();
void setMotionBlurStrength(float strength);
float motionBlurStrength();
void resetMotionBlur();

// Stubs de compatibilidad legacy
inline void setGhostRecording(bool on) { setMotionBlurEnabled(on); }
inline bool ghostRecording() { return motionBlurEnabled(); }
inline void* ghostSrv(int) { return nullptr; }
inline int ghostFilled() { return 0; }

}
