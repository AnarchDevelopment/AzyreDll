#pragma once

// Estado interno compartido del BlurHelper: SOLO lo incluyen los .cpp del
// propio helper. Las definiciones viven en los archivos duenos de cada pieza.

#include "Framework/Log.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <windows.h>

namespace mc::blur::detail {

// Propiedad de BlurHelper.cpp.
extern ID3D11Device* g_device;
extern ID3D11DeviceContext* g_context;
extern IDXGISwapChain* g_swapChain;
extern float g_displayW;
extern float g_displayH;

// Propiedad de BlurTextures.cpp.
struct BlurTex
{
    ID3D11Texture2D* tex = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    UINT w = 0;
    UINT h = 0;
};

extern BlurTex g_btHalf;     // 1/2 de pantalla
extern BlurTex g_btQuarter;  // 1/4
extern BlurTex g_btSmall;    // 1/8
extern BlurTex g_btTiny;     // 1/16
extern BlurTex g_btGaussA;   // ping-pong gaussiano a 1/16
extern BlurTex g_btGaussB;   // resultado final del blur
extern BlurTex g_btProxy;    // render proxy full-res (experimental)

// MotionBlur (lógica arst-motion-blur: acumulación full-res EMA)
extern BlurTex g_mbPrev;       // Frame acumulado anterior a resolución completa
extern BlurTex g_mbSwap;       // Buffer temporal para blend/swap a resolución completa
extern bool g_mbHasHistory;    // Indica si g_mbPrev contiene datos válidos del frame previo

// Propiedad de BlurShaders.cpp.
extern ID3D11VertexShader* g_blurVS;
extern ID3D11PixelShader* g_blurPS;
extern ID3D11PixelShader* g_copyPS;
extern ID3D11PixelShader* g_compositePS;
extern ID3D11PixelShader* g_glassPS;
extern ID3D11PixelShader* g_motionBlurPS;
extern ID3D11BlendState* g_blurBlend;
extern ID3D11Buffer* g_blurCB;
extern ID3D11SamplerState* g_blurSampler;
extern ID3D11RasterizerState* g_blurRS;
extern ID3D11RasterizerState* g_blurScissorRS;
extern bool g_shadersOk;

// Propiedad de BlurCapture.cpp.
extern ID3D11Texture2D* g_captureTex;    // clon full-res del backbuffer
extern ID3D11ShaderResourceView* g_captureSrv;
extern UINT g_captureW;
extern UINT g_captureH;

} // namespace mc::blur::detail

// Los pases de backdrop corren a full rate con el menu abierto: los errores
// se loguean UNA sola vez para no inundar el log.
#define BD_LOG_ONCE(flag, ...)                                                                     \
    do                                                                                             \
    {                                                                                              \
        if (!(flag))                                                                               \
        {                                                                                          \
            (flag) = true;                                                                         \
            MC_LOG_ERROR(__VA_ARGS__);                                                             \
        }                                                                                          \
    } while (0)

// Errores recurrentes (ej. captura que falla cada frame): log periodico
// para poder diagnosticar congelamientos que el one-shot esconde.
#define BD_LOG_EVERY(flag, intervalMs, ...)                                                         \
    do                                                                                             \
    {                                                                                              \
        ULONGLONG bdNow = GetTickCount64();                                                         \
        if (!(flag) || bdNow - (flag) >= (intervalMs))                                              \
        {                                                                                          \
            (flag) = bdNow;                                                                         \
            MC_LOG_ERROR(__VA_ARGS__);                                                             \
        }                                                                                          \
    } while (0)
