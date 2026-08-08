/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <string>
#include <map>
#include <d3d11.h>

class ClickGUI {
public:
    static bool g_enabled;
    static int g_guiStyle;        // 0 = Regular (Default), 1 = Separated, 2 = Rise, 3 = Lunar
    static bool g_showParticles;
    static float g_bgOpacity;

    // Background style: 0 = Normal (dark overlay), 1 = Mica Blur (default)
    static int g_bgStyle;
    static float g_blurRadius;    // Blur strength (default 4.0)
    static float g_blurOpacity;   // How opaque the Mica tint feels

    // Expanded settings states in Separated UI
    static std::map<std::string, bool> g_expandedModules;

    // Initialize ClickGUI settings
    static void Initialize();

    // Render ClickGUI module settings in the standard GUI
    static void RenderMenu();

    // Render Separated Menu Layout
    static void RenderSeparatedMenu(float screenWidth, float screenHeight);

    // Render Rise Menu Layout
    static void RenderRiseMenu(float screenWidth, float screenHeight);

    // Render Lunar Menu Layout
    static void RenderLunarMenu(float screenWidth, float screenHeight);

    // Helper to render module buttons
    static void RenderModuleButton(const char* label, bool* enabledPtr, void (*toggleCallback)() = nullptr);

    // Helper to render expanded module settings
    static void RenderModuleSettings(const char* name, float colWidth);

    // --- Mica Blur Background ---
    // Call once per frame when menu is open (captures scene, renders Mica-style blur)
    static void InitializeBlurShaders(ID3D11Device* pDevice);
    static void ShutdownBlurShaders();
    static void RenderBlurBackground(ID3D11Device* pDevice, ID3D11DeviceContext* pContext,
                                     IDXGISwapChain* pSwapChain,
                                     float screenWidth, float screenHeight, float menuAnim);

    // Region-scoped Mica blur (frosted glass behind HUD elements like the ArrayList).
    // Blurs the captured scene but only writes pixels inside the given screen rect.
    static void RenderBlurRegion(ID3D11Device* pDevice, ID3D11DeviceContext* pContext,
                                 IDXGISwapChain* pSwapChain,
                                 float screenWidth, float screenHeight,
                                 float rectX, float rectY, float rectW, float rectH,
                                 float radius, float opacity);

    static bool g_blurShadersReady;

private:
    // DX11 Mica blur pipeline state
    static ID3D11PixelShader*          g_downscalePS;   // 4x1 box downsample pass
    static ID3D11PixelShader*          g_blurHPS;       // horizontal gaussian pass
    static ID3D11PixelShader*          g_blurVPS;       // vertical gaussian pass
    static ID3D11PixelShader*          g_compositePS;   // Mica tint composite pass
    static ID3D11VertexShader*         g_blurVS;
    static ID3D11InputLayout*          g_blurIL;
    static ID3D11Buffer*               g_blurVB;
    static ID3D11Buffer*               g_blurCB;
    static ID3D11SamplerState*         g_blurSampler;
    static ID3D11BlendState*           g_blurBlendState;
    static ID3D11DepthStencilState*    g_blurDSS;
    static ID3D11RasterizerState*      g_blurRS;
    static ID3D11RasterizerState*      g_blurScissorRS;

    // Scene capture (full resolution)
    static ID3D11ShaderResourceView*   g_sceneSRV;
    static ID3D11Texture2D*            g_sceneTexture;
    static UINT                        g_capturedWidth;
    static UINT                        g_capturedHeight;

    // Half-resolution work textures (downsample -> blurH -> blurV)
    static ID3D11Texture2D*            g_workDownTexture;
    static ID3D11ShaderResourceView*   g_workDownSRV;
    static ID3D11RenderTargetView*     g_workDownRTV;
    static ID3D11Texture2D*            g_workBlurHTexture;
    static ID3D11ShaderResourceView*   g_workBlurHSRV;
    static ID3D11RenderTargetView*     g_workBlurHRTV;
    static ID3D11Texture2D*            g_workBlurVTexture;
    static ID3D11ShaderResourceView*   g_workBlurVSRV;
    static ID3D11RenderTargetView*     g_workBlurVRTV;
    static UINT                        g_workWidth;
    static UINT                        g_workHeight;

    static bool CompileBlurShader(const char* src, const char* entry,
                                  const char* model, ID3DBlob** blob);
    static bool CreateWorkTexture(ID3D11Device* pDevice, UINT width, UINT height, DXGI_FORMAT format,
                                  ID3D11Texture2D** ppTex, ID3D11ShaderResourceView** ppSRV,
                                  ID3D11RenderTargetView** ppRTV);

    // Shared blur pipeline. `scissor` restricts the composite pass to a screen region.
    static void RenderBlurInternal(ID3D11Device* pDevice, ID3D11DeviceContext* pContext,
                                   IDXGISwapChain* pSwapChain,
                                   float screenWidth, float screenHeight,
                                   const float* tint, float blurRadius, float blurOp,
                                   const D3D11_RECT* scissor);
};
