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
    static int g_guiStyle;        // 0 = Regular (Default), 1 = Separated, 2 = Rise
    static bool g_showParticles;
    static float g_bgOpacity;

    // Background style: 0 = Normal (dark overlay), 1 = Blurred Background
    static int g_bgStyle;
    static float g_blurRadius;    // Blur strength (default 4.0)
    static float g_blurOpacity;   // How opaque the blur overlay feels

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

    // Helper to render module buttons
    static void RenderModuleButton(const char* label, bool* enabledPtr, void (*toggleCallback)() = nullptr);

    // Helper to render expanded module settings
    static void RenderModuleSettings(const char* name, float colWidth);

    // --- Blur Background ---
    // Call once per frame when menu is open (captures scene, renders blur)
    static void InitializeBlurShaders(ID3D11Device* pDevice);
    static void ShutdownBlurShaders();
    static void RenderBlurBackground(ID3D11Device* pDevice, ID3D11DeviceContext* pContext,
                                     IDXGISwapChain* pSwapChain,
                                     float screenWidth, float screenHeight, float menuAnim);

    static bool g_blurShadersReady;

private:
    // DX11 blur pipeline state
    static ID3D11PixelShader*          g_blurPS;
    static ID3D11VertexShader*         g_blurVS;
    static ID3D11InputLayout*          g_blurIL;
    static ID3D11Buffer*               g_blurVB;
    static ID3D11Buffer*               g_blurCB;
    static ID3D11SamplerState*         g_blurSampler;
    static ID3D11BlendState*           g_blurBlendState;
    static ID3D11DepthStencilState*    g_blurDSS;
    static ID3D11RasterizerState*      g_blurRS;

    // Scene capture
    static ID3D11ShaderResourceView*   g_sceneSRV;
    static ID3D11Texture2D*            g_sceneTexture;
    static UINT                        g_capturedWidth;
    static UINT                        g_capturedHeight;

    static bool CompileBlurShader(const char* src, const char* entry,
                                  const char* model, ID3DBlob** blob);
};
