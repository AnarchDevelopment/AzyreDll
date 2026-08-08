/*
Under an4rch Development Public Source License 1.0
*/
#define IMGUI_DEFINE_MATH_OPERATORS
#include "ClickGUI.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../GUI/GUI.hpp"
#include "../../../Animations/Animations.hpp"
#include "../../ModuleHeader.hpp"
#include "../../Globals.hpp"
#include "../../../ArrayList/ArrayList.hpp"
#include "../../../Networking/IRChat.hpp"
#include "../../../Config/ConfigManager.hpp"
#include "../../../Assets/resource.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <map>
#include <vector>
#include <string>
#include <cstring>
#include <cctype>

extern HMODULE g_hModule;

// ──────────────────────────────────────────────
// Static member initialization
// ──────────────────────────────────────────────
bool  ClickGUI::g_enabled        = false;
int   ClickGUI::g_guiStyle       = 0;
bool  ClickGUI::g_showParticles  = true;
float ClickGUI::g_bgOpacity      = 0.7f;
int   ClickGUI::g_bgStyle        = 1;   // 0 = Normal dark, 1 = Mica Blur (default)
float ClickGUI::g_blurRadius     = 2.5f;
float ClickGUI::g_blurOpacity    = 0.25f;
bool  ClickGUI::g_blurShadersReady = false;
std::map<std::string, bool> ClickGUI::g_expandedModules;

// DX11 Mica blur pipeline
ID3D11PixelShader*       ClickGUI::g_downscalePS   = nullptr;
ID3D11PixelShader*       ClickGUI::g_blurHPS       = nullptr;
ID3D11PixelShader*       ClickGUI::g_blurVPS       = nullptr;
ID3D11PixelShader*       ClickGUI::g_compositePS   = nullptr;
ID3D11VertexShader*      ClickGUI::g_blurVS        = nullptr;
ID3D11InputLayout*       ClickGUI::g_blurIL        = nullptr;
ID3D11Buffer*            ClickGUI::g_blurVB        = nullptr;
ID3D11Buffer*            ClickGUI::g_blurCB        = nullptr;
ID3D11SamplerState*      ClickGUI::g_blurSampler   = nullptr;
ID3D11BlendState*        ClickGUI::g_blurBlendState = nullptr;
ID3D11DepthStencilState* ClickGUI::g_blurDSS       = nullptr;
ID3D11RasterizerState*   ClickGUI::g_blurRS        = nullptr;
ID3D11RasterizerState*   ClickGUI::g_blurScissorRS = nullptr;
ID3D11ShaderResourceView* ClickGUI::g_sceneSRV     = nullptr;
ID3D11Texture2D*         ClickGUI::g_sceneTexture  = nullptr;
UINT ClickGUI::g_capturedWidth  = 0;
UINT ClickGUI::g_capturedHeight = 0;
ID3D11Texture2D*         ClickGUI::g_workDownTexture   = nullptr;
ID3D11ShaderResourceView* ClickGUI::g_workDownSRV      = nullptr;
ID3D11RenderTargetView*  ClickGUI::g_workDownRTV       = nullptr;
ID3D11Texture2D*         ClickGUI::g_workBlurHTexture  = nullptr;
ID3D11ShaderResourceView* ClickGUI::g_workBlurHSRV     = nullptr;
ID3D11RenderTargetView*  ClickGUI::g_workBlurHRTV      = nullptr;
ID3D11Texture2D*         ClickGUI::g_workBlurVTexture  = nullptr;
ID3D11ShaderResourceView* ClickGUI::g_workBlurVSRV     = nullptr;
ID3D11RenderTargetView*  ClickGUI::g_workBlurVRTV      = nullptr;
UINT ClickGUI::g_workWidth  = 0;
UINT ClickGUI::g_workHeight = 0;

// ──────────────────────────────────────────────
// Inline HLSL sources (embedded, no .hlsl file at runtime)
// ──────────────────────────────────────────────
static const char* s_blurVsSrc = R"(
struct VS_INPUT  { float3 Pos : POSITION; float2 Tex : TEXCOORD0; };
struct VS_OUTPUT { float4 Pos : SV_POSITION; float2 Tex : TEXCOORD0; };
VS_OUTPUT mainVS(VS_INPUT input) {
    VS_OUTPUT o;
    o.Pos = float4(input.Pos, 1.0);
    o.Tex = input.Tex;
    return o;
}
)";

static const char* s_blurPsSrc = R"(
cbuffer BlurParams : register(b0) {
    float2 texelSize;   // 1/sourceW, 1/sourceH
    float  blurRadius;  // spread in texels
    float  opacity;     // Mica tint strength
    float4 tintColor;   // Mica tint color (rgb), a unused
    float2 pad;         // 16-byte alignment
};
Texture2D    g_scene   : register(t0);
SamplerState g_sampler : register(s0);
struct VS_OUTPUT { float4 Pos : SV_POSITION; float2 Tex : TEXCOORD0; };

static const int SAMPLES = 13;
static const float offsets[13] = { -6,-5,-4,-3,-2,-1, 0, 1, 2, 3, 4, 5, 6 };
static const float weights[13] = {
    0.002216, 0.008764, 0.026995, 0.064759, 0.120985, 0.176033,
    0.199471,
    0.176033, 0.120985, 0.064759, 0.026995, 0.008764, 0.002216
};

// 4-tap box downsample
float4 mainDownPS(VS_OUTPUT input) : SV_Target {
    float2 t = texelSize;
    float4 col = g_scene.Sample(g_sampler, input.Tex + float2(-0.5, -0.5) * t);
    col += g_scene.Sample(g_sampler, input.Tex + float2( 0.5, -0.5) * t);
    col += g_scene.Sample(g_sampler, input.Tex + float2(-0.5,  0.5) * t);
    col += g_scene.Sample(g_sampler, input.Tex + float2( 0.5,  0.5) * t);
    return col * 0.25;
}

// Horizontal gaussian blur
float4 mainBlurHPS(VS_OUTPUT input) : SV_Target {
    float4 col = float4(0,0,0,0);
    [unroll]
    for (int i = 0; i < SAMPLES; i++) {
        col += g_scene.Sample(g_sampler, input.Tex + float2(offsets[i] * blurRadius * texelSize.x, 0.0)) * weights[i];
    }
    return col;
}

// Vertical gaussian blur
float4 mainBlurVPS(VS_OUTPUT input) : SV_Target {
    float4 col = float4(0,0,0,0);
    [unroll]
    for (int i = 0; i < SAMPLES; i++) {
        col += g_scene.Sample(g_sampler, input.Tex + float2(0.0, offsets[i] * blurRadius * texelSize.y)) * weights[i];
    }
    return col;
}

// ── Win11-style Mica composite ──
// Frosted material: desaturated blur + neutral base tint (theme accent hint) +
// subtle elevation gradient + fine frosted-glass grain.

static float hash2(float2 p) {
    p = frac(p * 0.1031);
    p += dot(p, p.yx + 33.33);
    return frac((p.x + p.y) * p.x);
}

static float valueNoise(float2 uv) {
    float2 i = floor(uv);
    float2 f = frac(uv);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash2(i);
    float b = hash2(i + float2(1.0, 0.0));
    float c = hash2(i + float2(0.0, 1.0));
    float d = hash2(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float4 mainCompositePS(VS_OUTPUT input) : SV_Target {
    float2 uv = input.Tex;
    float3 col = g_scene.Sample(g_sampler, uv).rgb;

    // Mica frosted desaturation: mostly neutral, hint of color retained
    float luma = dot(col, float3(0.299, 0.587, 0.114));
    col = lerp(float3(luma, luma, luma), col, 0.82);

    // Neutral Mica material base tinted by the theme accent (computed on CPU)
    col = lerp(col, tintColor.rgb, opacity);

    // Very subtle elevation gradient: slight brightening toward the top
    col += float3(0.004, 0.004, 0.005) * (1.0 - uv.y);

    // Whisper of frosted-glass grain: smooth, fine and barely perceptible
    float2 grain = uv * (1.0 / texelSize) / 192.0;
    col += (valueNoise(grain) - 0.5) * 0.006;

    return float4(col, 1.0);
}
)";

// ──────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────
bool ClickGUI::CompileBlurShader(const char* src, const char* entry,
                                  const char* model, ID3DBlob** blob) {
    ID3DBlob* err = nullptr;
    HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr,
                             entry, model, 0, 0, blob, &err);
    if (FAILED(hr)) {
        if (err) { OutputDebugStringA((char*)err->GetBufferPointer()); err->Release(); }
        return false;
    }
    if (err) err->Release();
    return true;
}

// Convert a possibly typeless/sRGB backbuffer format into a typed SRV-able format.
// The scene resource keeps the backbuffer format (required by CopyResource) but the
// SRV must use a fully-typed format, otherwise CreateShaderResourceView fails and the
// whole blur pipeline silently never renders.
static DXGI_FORMAT MakeSrvFormat(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:       return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:       return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:    return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:   return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R32G32B32A32_TYPELESS:   return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case DXGI_FORMAT_R8_TYPELESS:             return DXGI_FORMAT_R8_UNORM;
        case DXGI_FORMAT_R16_TYPELESS:            return DXGI_FORMAT_R16_FLOAT;
        case DXGI_FORMAT_R32_TYPELESS:            return DXGI_FORMAT_R32_FLOAT;
        default:                                  return f;
    }
}

// ──────────────────────────────────────────────
// Initialize / Shutdown Mica blur pipeline
// ──────────────────────────────────────────────
bool ClickGUI::CreateWorkTexture(ID3D11Device* pDevice, UINT width, UINT height, DXGI_FORMAT format,
                                 ID3D11Texture2D** ppTex, ID3D11ShaderResourceView** ppSRV,
                                 ID3D11RenderTargetView** ppRTV) {
    if (!pDevice || !ppTex || !ppSRV || !ppRTV) return false;
    if (width  == 0) width  = 1;
    if (height == 0) height = 1;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width            = width;
    td.Height           = height;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = format;
    td.SampleDesc.Count = 1;
    td.Usage            = D3D11_USAGE_DEFAULT;
    td.BindFlags        = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    td.CPUAccessFlags   = 0;
    td.MiscFlags        = 0;
    if (FAILED(pDevice->CreateTexture2D(&td, nullptr, ppTex))) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
    srvd.Format                    = format;
    srvd.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels       = 1;
    srvd.Texture2D.MostDetailedMip = 0;
    if (FAILED(pDevice->CreateShaderResourceView(*ppTex, &srvd, ppSRV))) {
        (*ppTex)->Release(); *ppTex = nullptr;
        return false;
    }

    D3D11_RENDER_TARGET_VIEW_DESC rtvd = {};
    rtvd.Format             = format;
    rtvd.ViewDimension      = D3D11_RTV_DIMENSION_TEXTURE2D;
    rtvd.Texture2D.MipSlice = 0;
    if (FAILED(pDevice->CreateRenderTargetView(*ppTex, &rtvd, ppRTV))) {
        (*ppSRV)->Release(); *ppSRV = nullptr;
        (*ppTex)->Release();  *ppTex = nullptr;
        return false;
    }
    return true;
}

void ClickGUI::InitializeBlurShaders(ID3D11Device* pDevice) {
    if (g_blurShadersReady || !pDevice) return;

    // --- Vertex Shader ---
    ID3DBlob* vsBlob = nullptr;
    if (!CompileBlurShader(s_blurVsSrc, "mainVS", "vs_5_0", &vsBlob)) return;
    pDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &g_blurVS);

    // --- Input Layout ---
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,                            D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    pDevice->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &g_blurIL);
    vsBlob->Release();

    // --- Pixel Shaders (downsample / blur H / blur V / composite) ---
    ID3DBlob* psBlob = nullptr;
    if (CompileBlurShader(s_blurPsSrc, "mainDownPS", "ps_5_0", &psBlob)) {
        pDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_downscalePS);
        psBlob->Release();
    }
    if (CompileBlurShader(s_blurPsSrc, "mainBlurHPS", "ps_5_0", &psBlob)) {
        pDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_blurHPS);
        psBlob->Release();
    }
    if (CompileBlurShader(s_blurPsSrc, "mainBlurVPS", "ps_5_0", &psBlob)) {
        pDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_blurVPS);
        psBlob->Release();
    }
    if (CompileBlurShader(s_blurPsSrc, "mainCompositePS", "ps_5_0", &psBlob)) {
        pDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_compositePS);
        psBlob->Release();
    }

    if (!g_downscalePS || !g_blurHPS || !g_blurVPS || !g_compositePS) {
        ShutdownBlurShaders();
        return;
    }

    // --- Full-screen quad vertex buffer (NDC, flipped Y UVs for DX) ---
    struct Vtx { float x, y, z, u, v; };
    Vtx verts[] = {
        {-1, -1, 0,  0, 1},
        {-1,  1, 0,  0, 0},
        { 1, -1, 0,  1, 1},
        { 1,  1, 0,  1, 0},
    };
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth      = sizeof(verts);
    vbDesc.Usage          = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData = { verts };
    pDevice->CreateBuffer(&vbDesc, &vbData, &g_blurVB);

    // --- Constant Buffer (12 floats = 48 bytes: texelSize + radius + opacity + tint + pad) ---
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth      = sizeof(float) * 12;
    cbDesc.Usage          = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    pDevice->CreateBuffer(&cbDesc, nullptr, &g_blurCB);

    // --- Sampler ---
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    pDevice->CreateSamplerState(&sd, &g_blurSampler);

    // --- Blend state (opaque output) ---
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = FALSE;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    pDevice->CreateBlendState(&bd, &g_blurBlendState);

    // --- Depth stencil state (no depth test) ---
    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable = FALSE;
    pDevice->CreateDepthStencilState(&dsd, &g_blurDSS);

    // --- Rasterizer state (no culling) ---
    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    pDevice->CreateRasterizerState(&rd, &g_blurRS);

    // --- Rasterizer state with scissor enabled (for region-scoped blur) ---
    rd.ScissorEnable = TRUE;
    pDevice->CreateRasterizerState(&rd, &g_blurScissorRS);

    g_blurShadersReady = true;
}

void ClickGUI::ShutdownBlurShaders() {
    auto safe = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    safe(g_downscalePS); safe(g_blurHPS); safe(g_blurVPS); safe(g_compositePS);
    safe(g_blurVS); safe(g_blurIL); safe(g_blurVB);
    safe(g_blurCB); safe(g_blurSampler); safe(g_blurBlendState);
    safe(g_blurDSS); safe(g_blurRS); safe(g_blurScissorRS); safe(g_sceneSRV); safe(g_sceneTexture);
    safe(g_workDownTexture); safe(g_workDownSRV); safe(g_workDownRTV);
    safe(g_workBlurHTexture); safe(g_workBlurHSRV); safe(g_workBlurHRTV);
    safe(g_workBlurVTexture); safe(g_workBlurVSRV); safe(g_workBlurVRTV);
    g_capturedWidth = 0; g_capturedHeight = 0;
    g_workWidth = 0; g_workHeight = 0;
    g_blurShadersReady = false;
}

// ──────────────────────────────────────────────
// Render Mica-style blur over the full screen
// ──────────────────────────────────────────────
void ClickGUI::RenderBlurBackground(ID3D11Device* pDevice, ID3D11DeviceContext* pContext,
                                     IDXGISwapChain* pSwapChain,
                                     float screenWidth, float screenHeight, float menuAnim) {
    if (!pDevice || !pContext || !pSwapChain) return;
    if (menuAnim <= 0.001f) return;

    float e = Animations::EaseOutQuart(menuAnim);
    if (e <= 0.001f) return;

    // Win11-style Mica material base: neutral dark gray with a subtle theme accent hint
    float tint[3] = {
        0.13f + GUI::g_colorAccent.x * 0.03f,
        0.13f + GUI::g_colorAccent.y * 0.03f,
        0.15f + GUI::g_colorAccent.z * 0.04f
    };
    // Work textures are half-res, so halve the radius to keep the same visual spread
    float blurRadius = g_blurRadius * e * 0.5f;
    float blurOp     = g_blurOpacity * e;
    if (blurOp > 1.0f) blurOp = 1.0f;

    RenderBlurInternal(pDevice, pContext, pSwapChain, screenWidth, screenHeight,
                       tint, blurRadius, blurOp, nullptr);
}

// ──────────────────────────────────────────────
// Region-scoped Mica blur (frosted glass behind HUD elements)
// ──────────────────────────────────────────────
void ClickGUI::RenderBlurRegion(ID3D11Device* pDevice, ID3D11DeviceContext* pContext,
                                 IDXGISwapChain* pSwapChain,
                                 float screenWidth, float screenHeight,
                                 float rectX, float rectY, float rectW, float rectH,
                                 float radius, float opacity) {
    if (!pDevice || !pContext || !pSwapChain) return;
    if (rectW <= 0.0f || rectH <= 0.0f) return;

    float tint[3] = {
        0.13f + GUI::g_colorAccent.x * 0.03f,
        0.13f + GUI::g_colorAccent.y * 0.03f,
        0.15f + GUI::g_colorAccent.z * 0.04f
    };
    float blurOp = opacity;
    if (blurOp > 1.0f) blurOp = 1.0f;

    D3D11_RECT scissor = {
        (LONG)rectX, (LONG)rectY, (LONG)(rectX + rectW), (LONG)(rectY + rectH)
    };

    RenderBlurInternal(pDevice, pContext, pSwapChain, screenWidth, screenHeight,
                       tint, radius, blurOp, &scissor);
}

// Shared blur pipeline. `scissor` restricts the composite pass to a screen region.
void ClickGUI::RenderBlurInternal(ID3D11Device* pDevice, ID3D11DeviceContext* pContext,
                                   IDXGISwapChain* pSwapChain,
                                   float screenWidth, float screenHeight,
                                   const float* tint, float blurRadius, float blurOp,
                                   const D3D11_RECT* scissor) {
    if (!pDevice || !pContext || !pSwapChain) return;

    // Lazy re-init: if the first initialization failed (e.g. shader compile not ready),
    // retry here so the blur doesn't silently stay disabled forever.
    if (!g_blurShadersReady) {
        InitializeBlurShaders(pDevice);
        if (!g_blurShadersReady) return;
    }

    // ---- Capture current backbuffer (scene before ImGui) ----
    ID3D11Texture2D* pBack = nullptr;
    if (FAILED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBack))) return;

    D3D11_TEXTURE2D_DESC desc;
    pBack->GetDesc(&desc);

    // Re-create capture texture only if resolution changed (or the SRV was never created)
    if (!g_sceneTexture || !g_sceneSRV || g_capturedWidth != desc.Width || g_capturedHeight != desc.Height) {
        if (g_sceneSRV)      { g_sceneSRV->Release();      g_sceneSRV = nullptr; }
        if (g_sceneTexture)  { g_sceneTexture->Release();  g_sceneTexture = nullptr; }

        D3D11_TEXTURE2D_DESC td = desc;
        td.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
        td.Usage          = D3D11_USAGE_DEFAULT;
        td.CPUAccessFlags = 0;
        td.MiscFlags      = 0;
        if (FAILED(pDevice->CreateTexture2D(&td, nullptr, &g_sceneTexture))) {
            pBack->Release(); return;
        }
        DXGI_FORMAT srvFormat = MakeSrvFormat(desc.Format);
        D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
        srvd.Format                    = srvFormat;
        srvd.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvd.Texture2D.MipLevels       = 1;
        srvd.Texture2D.MostDetailedMip = 0;
        if (FAILED(pDevice->CreateShaderResourceView(g_sceneTexture, &srvd, &g_sceneSRV))) {
            g_sceneTexture->Release(); g_sceneTexture = nullptr;
            pBack->Release(); return;
        }
        g_capturedWidth  = desc.Width;
        g_capturedHeight = desc.Height;
    }

    // Half-resolution work textures (downsample -> blurH -> blurV)
    UINT w2 = max(1u, desc.Width  / 2);
    UINT h2 = max(1u, desc.Height / 2);
    if (!g_workDownTexture || g_workWidth != w2 || g_workHeight != h2) {
        auto releaseWork = [&]() {
            auto s = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
            s(g_workDownTexture);  s(g_workDownSRV);  s(g_workDownRTV);
            s(g_workBlurHTexture); s(g_workBlurHSRV); s(g_workBlurHRTV);
            s(g_workBlurVTexture); s(g_workBlurVSRV); s(g_workBlurVRTV);
            g_workWidth = 0; g_workHeight = 0;
        };
        releaseWork();
        if (!CreateWorkTexture(pDevice, w2, h2, DXGI_FORMAT_R8G8B8A8_UNORM, &g_workDownTexture,  &g_workDownSRV,  &g_workDownRTV) ||
            !CreateWorkTexture(pDevice, w2, h2, DXGI_FORMAT_R8G8B8A8_UNORM, &g_workBlurHTexture, &g_workBlurHSRV, &g_workBlurHRTV) ||
            !CreateWorkTexture(pDevice, w2, h2, DXGI_FORMAT_R8G8B8A8_UNORM, &g_workBlurVTexture, &g_workBlurVSRV, &g_workBlurVRTV)) {
            pBack->Release(); return;
        }
        g_workWidth = w2; g_workHeight = h2;
    }

    pContext->CopyResource(g_sceneTexture, pBack);

    // ---- Save current DX11 state ----
    ID3D11RenderTargetView* oldRTV = nullptr;
    ID3D11DepthStencilView* oldDSV = nullptr;
    pContext->OMGetRenderTargets(1, &oldRTV, &oldDSV);

    UINT oldStride, oldOffset;
    ID3D11Buffer* oldVB = nullptr;
    pContext->IAGetVertexBuffers(0, 1, &oldVB, &oldStride, &oldOffset);

    ID3D11InputLayout* oldIL = nullptr; pContext->IAGetInputLayout(&oldIL);
    D3D11_PRIMITIVE_TOPOLOGY oldTopo; pContext->IAGetPrimitiveTopology(&oldTopo);
    ID3D11VertexShader* oldVS = nullptr; pContext->VSGetShader(&oldVS, nullptr, nullptr);
    ID3D11PixelShader*  oldPS = nullptr; pContext->PSGetShader(&oldPS, nullptr, nullptr);
    ID3D11Buffer* oldCB = nullptr;       pContext->PSGetConstantBuffers(0, 1, &oldCB);
    ID3D11SamplerState* oldSS = nullptr; pContext->PSGetSamplers(0, 1, &oldSS);
    ID3D11ShaderResourceView* oldSRV = nullptr; pContext->PSGetShaderResources(0, 1, &oldSRV);
    ID3D11BlendState* oldBS = nullptr; float oldBF[4]; UINT oldSM;
    pContext->OMGetBlendState(&oldBS, oldBF, &oldSM);
    ID3D11DepthStencilState* oldDSS = nullptr; UINT oldRef;
    pContext->OMGetDepthStencilState(&oldDSS, &oldRef);
    ID3D11RasterizerState* oldRS = nullptr; pContext->RSGetState(&oldRS);

    UINT vpCount = 1; D3D11_VIEWPORT oldVP;
    pContext->RSGetViewports(&vpCount, &oldVP);

    // Composite target: always use a fresh RTV on the backbuffer itself.
    // Relying on the game's current RTV is fragile (it may be stale or null at
    // present time), which silently prevented the blur from being drawn.
    ID3D11RenderTargetView* compositeRTV = nullptr;
    ID3D11RenderTargetView* tempRTV = nullptr;
    pDevice->CreateRenderTargetView(pBack, nullptr, &tempRTV);
    compositeRTV = tempRTV ? tempRTV : oldRTV;
    if (!compositeRTV) {
        pBack->Release();
        auto sr2 = [](auto* p) { if (p) p->Release(); };
        sr2(oldRTV); sr2(oldDSV); sr2(oldVB); sr2(oldIL);
        sr2(oldVS); sr2(oldPS); sr2(oldCB); sr2(oldSS); sr2(oldSRV);
        sr2(oldBS); sr2(oldDSS); sr2(oldRS);
        return;
    }

    // ---- Shared pipeline setup ----
    UINT stride = sizeof(float) * 5, offset = 0;
    pContext->IASetVertexBuffers(0, 1, &g_blurVB, &stride, &offset);
    pContext->IASetInputLayout(g_blurIL);
    pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    pContext->VSSetShader(g_blurVS, nullptr, 0);
    pContext->PSSetConstantBuffers(0, 1, &g_blurCB);
    pContext->PSSetSamplers(0, 1, &g_blurSampler);
    pContext->OMSetBlendState(g_blurBlendState, nullptr, 0xFFFFFFFF);
    pContext->OMSetDepthStencilState(g_blurDSS, 0);
    pContext->RSSetState(scissor ? g_blurScissorRS : g_blurRS);
    if (scissor)
        pContext->RSSetScissorRects(1, scissor);

    auto SetCB = [&](float texW, float texH, float radius, float opacity, const float* tint) {
        D3D11_MAPPED_SUBRESOURCE ms;
        if (SUCCEEDED(pContext->Map(g_blurCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            float* d = (float*)ms.pData;
            d[0] = texW > 0.0f ? 1.0f / texW : 0.0f;
            d[1] = texH > 0.0f ? 1.0f / texH : 0.0f;
            d[2] = radius;
            d[3] = opacity;
            d[4] = tint ? tint[0] : 0.0f;
            d[5] = tint ? tint[1] : 0.0f;
            d[6] = tint ? tint[2] : 0.0f;
            d[7] = 1.0f;
            d[8] = 0.0f; d[9] = 0.0f;
            d[10] = 0.0f; d[11] = 0.0f;
            pContext->Unmap(g_blurCB, 0);
        }
    };

    // Win11-style Mica material base: neutral dark gray with a subtle theme accent hint
    // (dark-mode Mica base ~ #202020, not pure black, so the frosted layer reads clearly)

    // Pass 1: downscale full-res scene -> half-res
    {
        D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)w2, (float)h2, 0.0f, 1.0f };
        pContext->RSSetViewports(1, &vp);
        pContext->OMSetRenderTargets(1, &g_workDownRTV, nullptr);
        pContext->PSSetShader(g_downscalePS, nullptr, 0);
        ID3D11ShaderResourceView* src = g_sceneSRV;
        pContext->PSSetShaderResources(0, 1, &src);
        SetCB((float)desc.Width, (float)desc.Height, 0.0f, 0.0f, nullptr);
        pContext->Draw(4, 0);
    }

    // Pass 2: horizontal gaussian blur
    {
        D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)w2, (float)h2, 0.0f, 1.0f };
        pContext->RSSetViewports(1, &vp);
        pContext->OMSetRenderTargets(1, &g_workBlurHRTV, nullptr);
        pContext->PSSetShader(g_blurHPS, nullptr, 0);
        ID3D11ShaderResourceView* src = g_workDownSRV;
        pContext->PSSetShaderResources(0, 1, &src);
        SetCB((float)w2, (float)h2, blurRadius, 0.0f, nullptr);
        pContext->Draw(4, 0);
    }

    // Pass 3: vertical gaussian blur
    {
        D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)w2, (float)h2, 0.0f, 1.0f };
        pContext->RSSetViewports(1, &vp);
        pContext->OMSetRenderTargets(1, &g_workBlurVRTV, nullptr);
        pContext->PSSetShader(g_blurVPS, nullptr, 0);
        ID3D11ShaderResourceView* src = g_workBlurHSRV;
        pContext->PSSetShaderResources(0, 1, &src);
        SetCB((float)w2, (float)h2, blurRadius, 0.0f, nullptr);
        pContext->Draw(4, 0);
    }

    // Pass 4: composite blurred scene onto the screen with the Mica tint
    {
        D3D11_VIEWPORT vp = { 0.0f, 0.0f, screenWidth, screenHeight, 0.0f, 1.0f };
        pContext->RSSetViewports(1, &vp);
        pContext->OMSetRenderTargets(1, &compositeRTV, nullptr);
        pContext->PSSetShader(g_compositePS, nullptr, 0);
        ID3D11ShaderResourceView* src = g_workBlurVSRV;
        pContext->PSSetShaderResources(0, 1, &src);
        SetCB((float)w2, (float)h2, 0.0f, blurOp, tint);
        pContext->Draw(4, 0);
    }

    // ---- Restore state ----
    ID3D11ShaderResourceView* nullSRV = nullptr;
    pContext->PSSetShaderResources(0, 1, &nullSRV);

    pContext->OMSetRenderTargets(1, &oldRTV, oldDSV);
    UINT s2 = oldStride, o2 = oldOffset;
    pContext->IASetVertexBuffers(0, 1, &oldVB, &s2, &o2);
    pContext->IASetInputLayout(oldIL);
    pContext->IASetPrimitiveTopology(oldTopo);
    pContext->VSSetShader(oldVS, nullptr, 0);
    pContext->PSSetShader(oldPS, nullptr, 0);
    pContext->PSSetConstantBuffers(0, 1, &oldCB);
    pContext->PSSetSamplers(0, 1, &oldSS);
    pContext->PSSetShaderResources(0, 1, &oldSRV);
    pContext->OMSetBlendState(oldBS, oldBF, oldSM);
    pContext->OMSetDepthStencilState(oldDSS, oldRef);
    pContext->RSSetState(oldRS);
    pContext->RSSetViewports(vpCount, &oldVP);
    if (scissor) {
        // Clear any scissor we may have left enabled
        D3D11_RECT fullSc = { 0, 0, (LONG)screenWidth, (LONG)screenHeight };
        pContext->RSSetScissorRects(1, &fullSc);
    }

    // Release saved refs
    auto sr = [](auto* p) { if (p) p->Release(); };
    sr(oldRTV); sr(oldDSV); sr(oldVB); sr(oldIL);
    sr(oldVS); sr(oldPS); sr(oldCB); sr(oldSS); sr(oldSRV);
    sr(oldBS); sr(oldDSS); sr(oldRS);
    if (tempRTV) tempRTV->Release();
    pBack->Release();
}

// ──────────────────────────────────────────────
// Module lifecycle
// ──────────────────────────────────────────────
void ClickGUI::Initialize() {
    g_enabled       = false;
    g_guiStyle      = 0;
    g_showParticles = true;
    g_bgOpacity     = 0.7f;
    g_bgStyle       = 1;   // 1 = Mica Blur (default)
    g_blurRadius    = 2.5f;
    g_blurOpacity   = 0.25f;
    g_expandedModules.clear();
}

void ClickGUI::RenderMenu() {
    bool before = g_enabled;
    GUI::RenderCustomSwitch("ClickGUI Module", &g_enabled);
    if (g_enabled != before) {
        g_showMenu = g_enabled;
        GUI::g_showMenu = g_enabled;
    }

    if (GUI::BeginModuleSettings("ClickGUI", &g_enabled)) {
        const char* styles[] = { "Regular", "Separated", "Rise", "Lunar" };
        ImGui::Combo("GUI Style", &g_guiStyle, styles, IM_ARRAYSIZE(styles));

        const char* bgStyles[] = { "Normal Dark", "Mica Blur" };
        ImGui::Combo("Background", &g_bgStyle, bgStyles, IM_ARRAYSIZE(bgStyles));

        if (g_bgStyle == 1) {
            ImGui::SliderFloat("Blur Radius##CG", &g_blurRadius, 1.0f, 12.0f, "%.1f");
            ImGui::SliderFloat("Blur Opacity##CG", &g_blurOpacity, 0.0f, 1.0f, "%.2f");
        }

        GUI::RenderCustomSwitch("Plexus Background", &g_showParticles);

        const char* themes[] = { "Aegle Classic", "Sakura Blossom", "Cyberpunk 2077", "Emerald Forest", "Deep Sea" };
        int currentTheme = GUI::g_currentTheme;
        if (ImGui::Combo("Theme Preset", &currentTheme, themes, IM_ARRAYSIZE(themes))) {
            GUI::ApplyThemePreset(currentTheme);
        }
        GUI::EndModuleSettings();
    }
}

// ──────────────────────────────────────────────
// Separated style rendering
// ──────────────────────────────────────────────
void ClickGUI::RenderSeparatedMenu(float screenWidth, float screenHeight) {
    float e = Animations::EaseOutQuart(GUI::g_menuAnim);

    // Background (Normal or Blur handled via DX11 before ImGui frame, here just overlay)
    if (g_bgStyle == 0) {
        // Normal: dark tinted overlay
        ImU32 bgCol = IM_COL32(5, 5, 10, (int)(e * 180.0f));
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0,0), ImVec2(screenWidth, screenHeight), bgCol);
    } else {
        // Blurred: just a very light tint on top of the already-blurred DX11 blit
        ImU32 tint = IM_COL32(5, 5, 10, (int)(e * 60.0f));
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0,0), ImVec2(screenWidth, screenHeight), tint);
    }

    if (g_showParticles) {
        GUI::RenderParticles(ImGui::GetBackgroundDrawList(), ImVec2(0,0), ImVec2(screenWidth, screenHeight), e);
    }

    // Column layout with vertical slide animation
    float colWidth   = 220.0f;
    float spacing    = 20.0f;
    float totalWidth = 4.0f * colWidth + 3.0f * spacing;
    float startX     = (screenWidth - totalWidth) * 0.5f;
    float slideY     = (1.0f - e) * 80.0f;
    float startY     = screenHeight * 0.15f + slideY;

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, e);

    const char* categories[] = { "Combat", "Movement", "Visuals", "Misc" };

    struct Toggles {
        static void toggleReach()     { Reach::SetEnabled(Reach::g_reachEnabled); }
        static void toggleHitbox()    { if (Hitbox::g_hitboxEnabled) Hitbox::Enable(); else Hitbox::Disable(); }
        static void toggleTimer()     { if (Timer::g_timerEnabled)   Timer::Enable(); else   Timer::Disable(); }
        static void toggleFullBright(){ if (FullBright::g_fullBrightEnabled) FullBright::Enable(); else FullBright::Disable(); }
        static void toggleFPSOverlay() {
            if (FPSOverlay::g_showFpsOverlay) {
                FPSOverlay::g_fpsOverlayEnableTime  = GetTickCount64();
                FPSOverlay::g_fpsOverlayDisableTime = 0;
            } else {
                FPSOverlay::g_fpsOverlayDisableTime = GetTickCount64();
                FPSOverlay::g_fpsOverlayEnableTime  = 0;
            }
        }
        static void toggleClickGUI() { g_showMenu = ClickGUI::g_enabled; GUI::g_showMenu = g_showMenu; }
    };

    for (int i = 0; i < 4; ++i) {
        ImGui::SetNextWindowSize(ImVec2(colWidth, 0), ImGuiCond_Always);
        ImVec2 defaultPos = ImVec2(startX + i * (colWidth + spacing), startY);
        ImGui::SetNextWindowPos(defaultPos, ImGuiCond_FirstUseEver);

        std::string winName = std::string(categories[i]) + "##SepWin";

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,  ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.07f, 0.09f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.15f, 0.15f, 0.18f, 1.00f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

        if (ImGui::Begin(winName.c_str(), nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_AlwaysAutoResize)) {

            ImVec2 wPos  = ImGui::GetWindowPos();
            ImVec2 wSize = ImGui::GetWindowSize();
            ImDrawList* draw = ImGui::GetWindowDrawList();

            // Header
            float headerH = 42.0f;
            draw->AddRectFilled(wPos, ImVec2(wPos.x + wSize.x, wPos.y + headerH),
                                ImColor(12, 12, 15, 255), 12.0f, ImDrawFlags_RoundCornersTop);
            draw->AddLine(ImVec2(wPos.x, wPos.y + headerH),
                          ImVec2(wPos.x + wSize.x, wPos.y + headerH),
                          ImColor(GUI::g_colorAccent.x, GUI::g_colorAccent.y,
                                  GUI::g_colorAccent.z, 0.4f), 1.5f);

            // Category title
            ImGui::PushFont(GUI::g_fontH3 ? GUI::g_fontH3 : ImGui::GetFont());
            std::string catName = categories[i];
            for (auto& c : catName) c = toupper(c);
            ImVec2 textSize = ImGui::CalcTextSize(catName.c_str());
            ImVec2 textPos  = ImVec2(wPos.x + (wSize.x - textSize.x) * 0.5f,
                                     wPos.y + (headerH - textSize.y) * 0.5f);
            GUI::AddTextGlow(draw, ImGui::GetFont(), ImGui::GetFontSize(),
                             textPos, ImColor(255,255,255,255), catName.c_str(), 3.0f);
            ImGui::PopFont();

            ImGui::SetCursorPosY(headerH + 10.0f);

            // Module buttons per category
            if (i == 0) { // Combat
                RenderModuleButton("Reach",   &Reach::g_reachEnabled,  Toggles::toggleReach);
                RenderModuleButton("Hitbox",  &Hitbox::g_hitboxEnabled, Toggles::toggleHitbox);
            } else if (i == 1) { // Movement
                RenderModuleButton("AutoSprint", &AutoSprint::g_autoSprintEnabled);
                RenderModuleButton("Timer",       &Timer::g_timerEnabled, Toggles::toggleTimer);
            } else if (i == 2) { // Visuals
                RenderModuleButton("Watermark",   &Watermark::g_showWatermark);
                RenderModuleButton("ArrayList",   &ArrayList::g_enabled);
                RenderModuleButton("Render Info", &RenderInfo::g_showRenderInfo);
                RenderModuleButton("Keystrokes",  &Keystrokes::g_showKeystrokes);
                RenderModuleButton("CPS Counter", &CPSCounter::g_showCpsCounter);
                RenderModuleButton("FPS Overlay", &FPSOverlay::g_showFpsOverlay, Toggles::toggleFPSOverlay);
                RenderModuleButton("Ping Counter",&PingCounter::g_showPingCounter);
                RenderModuleButton("Player Info", &PlayerInfo::g_showPlayerInfo);
                RenderModuleButton("FullBright",  &FullBright::g_fullBrightEnabled, Toggles::toggleFullBright);
                RenderModuleButton("MotionBlur",  &MotionBlur::g_motionBlurEnabled);
                RenderModuleButton("ClickGUI",    &ClickGUI::g_enabled, Toggles::toggleClickGUI);
            } else if (i == 3) { // Misc
                RenderModuleButton("UnlockFPS", &UnlockFPS::g_unlockFpsEnabled);
                RenderModuleButton("AutoClicker", &AutoClicker::g_enabled);
                RenderModuleButton("Anti-AFK", &AntiAFK::g_enabled);
                RenderModuleButton("Screenshot", &Screenshot::g_enabled);
            }

            ImGui::Spacing();
        }
        ImGui::End();

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);
    }

    ImGui::PopStyleVar(); // Alpha
}

// ──────────────────────────────────────────────
// Module button rendering
// ──────────────────────────────────────────────

// True for modules whose settings are handled by RenderModuleSettings(). Modules
// without settings (AutoSprint, FullBright) get a plain toggle and cannot expand.
static bool ModuleHasSettings(const char* name) {
    return strcmp(name, "Reach") == 0 || strcmp(name, "Hitbox") == 0 ||
           strcmp(name, "Timer") == 0 || strcmp(name, "UnlockFPS") == 0 ||
           strcmp(name, "MotionBlur") == 0 || strcmp(name, "Watermark") == 0 ||
           strcmp(name, "ArrayList") == 0 || strcmp(name, "Render Info") == 0 ||
           strcmp(name, "Keystrokes") == 0 || strcmp(name, "CPS Counter") == 0 ||
           strcmp(name, "FPS Overlay") == 0 || strcmp(name, "Ping Counter") == 0 ||
           strcmp(name, "ClickGUI") == 0 || strcmp(name, "AutoClicker") == 0 ||
           strcmp(name, "Anti-AFK") == 0 || strcmp(name, "Screenshot") == 0 ||
           strcmp(name, "Player Info") == 0;
}

void ClickGUI::RenderModuleButton(const char* label, bool* enabledPtr, void (*toggleCallback)()) {
    ImGui::PushID(label);

    float btnHeight = 32.0f;
    float w = ImGui::GetContentRegionAvail().x - 16.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    bool active = *enabledPtr;

    // Enabled animation (frame-rate independent)
    std::string key = "btn_anim_" + std::string(label);
    if (GUI::g_elementAnims.find(key) == GUI::g_elementAnims.end())
        GUI::g_elementAnims[key] = active ? 1.0f : 0.0f;
    float target = active ? 1.0f : 0.0f;
    float dt = ImGui::GetIO().DeltaTime;
    GUI::g_elementAnims[key] = Animations::Approach(GUI::g_elementAnims[key], target, dt, 11.0f);
    float anim = GUI::g_elementAnims[key];

    ImGui::InvisibleButton(label, ImVec2(w, btnHeight));
    bool hovered      = ImGui::IsItemHovered();
    bool leftClicked  = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);

    // LEFT = toggle on/off
    if (leftClicked) {
        *enabledPtr = !*enabledPtr;
        if (toggleCallback) toggleCallback();
    }

    // RIGHT = expand/collapse settings (only modules that actually have settings)
    if (rightClicked && ModuleHasSettings(label)) {
        g_expandedModules[label] = !g_expandedModules[label];
    }

    // Draw background
    ImU32 bgCol = ImColor(30, 30, 36, (int)(220 + 35 * hovered));

    if (anim > 0.01f) {
        ImVec4 ac = GUI::g_colorAccent;
        draw->AddRectFilled(p, ImVec2(p.x + w, p.y + btnHeight),
            ImColor(ac.x, ac.y, ac.z, ac.w * anim), 6.0f);
    }
    if (anim < 0.99f) {
        draw->AddRectFilled(p, ImVec2(p.x + w, p.y + btnHeight), bgCol, 6.0f);
    }
    if (hovered) {
        draw->AddRect(p, ImVec2(p.x + w, p.y + btnHeight),
                      ImColor(255,255,255,30), 6.0f, 0, 1.0f);
    }

    // Label
    ImGui::PushFont(GUI::g_fontDefault);
    ImVec2 ts  = ImGui::CalcTextSize(label);
    ImVec2 tPos = ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (btnHeight - ts.y) * 0.5f);
    ImU32 textCol = ImColor(
        180.0f/255.0f + (75.0f/255.0f) * anim,
        180.0f/255.0f + (75.0f/255.0f) * anim,
        190.0f/255.0f + (65.0f/255.0f) * anim, 1.0f);
    draw->AddText(tPos, textCol, label);
    ImGui::PopFont();

    ImGui::PopID();

    // Animated expand/collapse settings with a proper panel: height + fade are
    // driven by a single frame-rate independent value; the natural content
    // height is measured once on the first expand so nothing ever gets clipped.
    std::string expandKey = "expand_" + std::string(label);
    bool expanded = ModuleHasSettings(label) && (g_expandedModules.count(label) ? g_expandedModules[label] : false);
    if (GUI::g_elementAnims.find(expandKey) == GUI::g_elementAnims.end())
        GUI::g_elementAnims[expandKey] = 0.0f;
    float expandTarget = expanded ? 1.0f : 0.0f;
    float ea = Animations::Approach(GUI::g_elementAnims[expandKey], expandTarget, dt, 9.0f);
    GUI::g_elementAnims[expandKey] = ea;

    if (ea > 0.01f) {
        float ease = Animations::EaseOutQuart(Animations::Clamp01(ea));
        float& measuredH = GUI::g_elementHeights[expandKey];
        if (measuredH <= 0.0f) measuredH = 1.0f;

        float panelH = measuredH * ease;
        if (panelH < 2.0f) panelH = 2.0f;

        ImGui::Spacing();
        ImVec2 panelStart = ImGui::GetCursorScreenPos();
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ease);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
        std::string cfgId = "##sepcfg_" + std::string(label);
        ImGui::BeginChild(cfgId.c_str(), ImVec2(w, panelH), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
        {
            ImGui::SetCursorPos(ImVec2(8.0f, 6.0f));
            ImGui::PushItemWidth(w - 24.0f);

            bool before = *enabledPtr;
            GUI::RenderCustomSwitch("Enabled", enabledPtr);
            if (*enabledPtr != before && toggleCallback) toggleCallback();

            ImGui::Spacing();
            RenderModuleSettings(label, w - 24.0f);

            ImGui::PopItemWidth();
            measuredH = ImGui::GetCursorPosY() + 12.0f;
        }
        ImGui::EndChild();

        ImGui::PopStyleVar();
        ImGui::PopStyleVar(); // alpha

        ImVec2 panelEnd = ImGui::GetItemRectMax();
        ImDrawList* wdraw = ImGui::GetWindowDrawList();
        ImVec4 ac = GUI::g_colorAccent;
        wdraw->AddRectFilled(panelStart, panelEnd, ImColor(17, 17, 22, (int)(195 * ease)), 6.0f);
        wdraw->AddRect(panelStart, panelEnd, ImColor(58, 58, 76, (int)(110 * ease)), 6.0f, 0, 1.0f);
        if (ease > 0.01f) {
            float barH = (panelEnd.y - panelStart.y) * ease;
            wdraw->AddRectFilled(
                ImVec2(panelStart.x + 1.5f, panelStart.y + 3.0f),
                ImVec2(panelStart.x + 3.5f, panelStart.y + barH - 3.0f),
                ImColor(ac.x, ac.y, ac.z, 0.9f * ease), 1.0f);
        }
        ImGui::Spacing();
    } else {
        ImGui::Spacing();
    }
}

// ──────────────────────────────────────────────
// Module inline settings
// ──────────────────────────────────────────────
void ClickGUI::RenderModuleSettings(const char* name, float /*colWidth*/) {
    if (strcmp(name, "Reach") == 0) {
        if (ImGui::SliderFloat("Distance##R", &Reach::g_reachValue, 3.0f, 15.0f, "%.2f"))
            Reach::UpdateValue(Reach::g_reachValue);
    } else if (strcmp(name, "Hitbox") == 0) {
        if (ImGui::SliderFloat("Size##H", &Hitbox::g_hitboxValue, 0.6f, 10.0f, "%.2f"))
            if (Hitbox::g_hitboxCave) memcpy((BYTE*)Hitbox::g_hitboxCave + 1, &Hitbox::g_hitboxValue, 4);
    } else if (strcmp(name, "Timer") == 0) {
        ImGui::SliderFloat("Speed##T", &Timer::g_timerValue, 0.1f, 20.0f, "%.1fx");
    } else if (strcmp(name, "UnlockFPS") == 0) {
        ImGui::SliderFloat("Limit##F", &UnlockFPS::g_fpsLimit, 0.0f, 1000.0f,
            UnlockFPS::g_fpsLimit <= 0.0f ? "Unlimited" : "%.0f");
    } else if (strcmp(name, "MotionBlur") == 0) {
        static int blurIdx = 0;
        static const char* types[] = {"Average Pixel Blur","Ghost Frames","Time Aware Blur","Real Motion Blur","V4"};
        for (int k = 0; k < 5; ++k) if (MotionBlur::g_blurType == types[k]) blurIdx = k;
        if (ImGui::Combo("Type##MB", &blurIdx, types, 5)) MotionBlur::g_blurType = types[blurIdx];
        if (MotionBlur::g_blurType == "Time Aware Blur") {
            ImGui::SliderFloat("Constant##MB", &MotionBlur::g_blurTimeConstant, 0.01f, 0.2f, "%.4f");
            ImGui::SliderFloat("Max History##MB", &MotionBlur::g_maxHistoryFrames, 4.0f, 16.0f, "%.0f");
        } else {
            ImGui::SliderFloat("Intensity##MB", &MotionBlur::g_blurIntensity, 1.0f, 30.0f, "%.0f");
        }
    } else if (strcmp(name, "Watermark") == 0) {
        GUI::RenderCustomSwitch("Chroma##WM",  &Watermark::g_chromaText);
        GUI::RenderCustomSwitch("Glow##WM",    &Watermark::g_showGlow);
        if (!Watermark::g_chromaText)
            ImGui::ColorEdit4("Color##WM", (float*)&Watermark::g_staticColor, ImGuiColorEditFlags_NoInputs);
    } else if (strcmp(name, "ArrayList") == 0) {
        ImGui::ColorEdit4("Bg##AL",    (float*)&ArrayList::g_bgColor, ImGuiColorEditFlags_NoInputs);
        ImGui::SliderFloat("Opacity##AL", &ArrayList::g_bgOpacity, 0.0f, 1.0f, "%.2f");
        GUI::RenderCustomSwitch("Side Bar##AL",  &ArrayList::g_showSideBar);
        if (ArrayList::g_showSideBar) {
            GUI::RenderCustomSwitch("Chroma SB##AL", &ArrayList::g_chromaSideBar);
            if (!ArrayList::g_chromaSideBar)
                ImGui::ColorEdit4("SB Color##AL", (float*)&ArrayList::g_sideBarColor, ImGuiColorEditFlags_NoInputs);
        }
        GUI::RenderCustomSwitch("Rounded##AL",   &ArrayList::g_roundedBorders);
        if (ArrayList::g_roundedBorders)
            ImGui::SliderFloat("Radius##AL", &ArrayList::g_borderRadius, 0.0f, 12.0f, "%.0f px");
        GUI::RenderCustomSwitch("Suffixes##AL",  &ArrayList::g_showSuffix);
    } else if (strcmp(name, "Render Info") == 0) {
        GUI::RenderCustomSwitch("Show Bg##RI", &RenderInfo::g_showBackground);
        ImGui::SliderFloat("Opacity##RI",    &RenderInfo::g_bgOpacity, 0.0f, 1.0f, "%.2f");
        ImGui::ColorEdit4("Theme Color##RI", (float*)&RenderInfo::g_staticColor, ImGuiColorEditFlags_NoInputs);
        ImGui::SliderFloat("Scale##RI",      &RenderInfo::g_scale, 0.5f, 2.0f, "%.1fx");
    } else if (strcmp(name, "Keystrokes") == 0) {
        ImGui::SliderFloat("Scale##KS",        &Keystrokes::g_keystrokesUIScale,  0.5f,  2.0f,  "%.2f");
        ImGui::SliderFloat("Rounding##KS",     &Keystrokes::g_keystrokesRounding, 0.0f,  20.0f, "%.1f");
        ImGui::SliderFloat("Key Spacing##KS",  &Keystrokes::g_keystrokesKeySpacing, 0.5f, 3.0f, "%.2f");
        ImGui::SliderFloat("Anim Speed##KS",   &Keystrokes::g_keystrokesEdSpeed,  0.1f,  5.0f,  "%.2f");
        ImGui::Spacing();
        GUI::RenderCustomSwitch("Show Bg##KS",        &Keystrokes::g_keystrokesShowBg);
        GUI::RenderCustomSwitch("Mouse Buttons##KS",  &Keystrokes::g_keystrokesShowMouseButtons);
        GUI::RenderCustomSwitch("Spacebar##KS",        &Keystrokes::g_keystrokesShowSpacebar);
        GUI::RenderCustomSwitch("LMB/RMB Style##KS",  &Keystrokes::g_keystrokesLMBRMB);
        GUI::RenderCustomSwitch("Hide CPS##KS",        &Keystrokes::g_keystrokesHideCPS);
        ImGui::Spacing();
        GUI::RenderCustomSwitch("Border##KS",     &Keystrokes::g_keystrokesBorder);
        if (Keystrokes::g_keystrokesBorder)
            ImGui::SliderFloat("Border Width##KS", &Keystrokes::g_keystrokesBorderWidth, 0.5f, 5.0f, "%.1f");
        GUI::RenderCustomSwitch("Text Shadow##KS",    &Keystrokes::g_keystrokesTextShadow);
        if (Keystrokes::g_keystrokesTextShadow)
            ImGui::SliderFloat("Shadow Offset##KS",   &Keystrokes::g_keystrokesTextShadowOffset, 0.001f, 0.02f, "%.3f");
        GUI::RenderCustomSwitch("Rect Shadow##KS",    &Keystrokes::g_keystrokesRectShadow);
        if (Keystrokes::g_keystrokesRectShadow)
            ImGui::SliderFloat("Rect Shadow Offset##KS", &Keystrokes::g_keystrokesRectShadowOffset, 0.005f, 0.1f, "%.3f");
        ImGui::Spacing();
        GUI::RenderCustomSwitch("Glow##KS",        &Keystrokes::g_keystrokesGlow);
        if (Keystrokes::g_keystrokesGlow)
            ImGui::SliderFloat("Glow Amount##KS",  &Keystrokes::g_keystrokesGlowAmount, 0.0f, 150.0f, "%.1f");
        GUI::RenderCustomSwitch("Glow Enabled##KS", &Keystrokes::g_keystrokesGlowEnabled);
        if (Keystrokes::g_keystrokesGlowEnabled) {
            ImGui::SliderFloat("Glow En. Amount##KS", &Keystrokes::g_keystrokesGlowEnabledAmount, 0.0f, 150.0f, "%.1f");
            ImGui::SliderFloat("Glow Speed##KS",      &Keystrokes::g_keystrokesGlowSpeed, 0.1f, 5.0f, "%.2f");
        }
        ImGui::Spacing();
        ImGui::SliderFloat("Text Scale##KS",     &Keystrokes::g_keystrokesTextScale,  0.3f, 2.0f, "%.2f");
        ImGui::SliderFloat("Text Scale2##KS",    &Keystrokes::g_keystrokesTextScale2, 0.3f, 2.0f, "%.2f");
        ImGui::SliderFloat("Text X Offset##KS",  &Keystrokes::g_keystrokesTextXOffset, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Text Y Offset##KS",  &Keystrokes::g_keystrokesTextYOffset, 0.0f, 1.0f, "%.2f");
        ImGui::Spacing();
        ImGui::ColorEdit4("Bg Color##KS",           &Keystrokes::g_keystrokesBgColor.x,          ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit4("Enabled Color##KS",      &Keystrokes::g_keystrokesEnabledColor.x,     ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit4("Text Color##KS",         &Keystrokes::g_keystrokesTextColor.x,         ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit4("Text En. Color##KS",     &Keystrokes::g_keystrokesTextEnabledColor.x, ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit4("Rect Shadow Color##KS",  &Keystrokes::g_keystrokesRectShadowColor.x,  ImGuiColorEditFlags_NoInputs);
    } else if (strcmp(name, "CPS Counter") == 0) {
        ImGui::SliderFloat("Scale##CPS", &CPSCounter::g_cpsTextScale, 0.5f, 2.0f, "%.2f");
        static const char* al[] = {"Left","Center","Right"};
        ImGui::Combo("Align##CPS", &CPSCounter::g_cpsCounterAlignment, al, 3);
        GUI::RenderCustomSwitch("Shadow##CPS", &CPSCounter::g_cpsCounterShadow);
        ImGui::ColorEdit4("Color##CPS", &CPSCounter::g_cpsTextColor.x, ImGuiColorEditFlags_NoInputs);
    } else if (strcmp(name, "FPS Overlay") == 0) {
        ImGui::SliderFloat("Scale##FO", &FPSOverlay::g_fpsTextScale, 0.5f, 2.0f, "%.1f");
        ImGui::ColorEdit4("Color##FO",  (float*)&FPSOverlay::g_fpsTextColor, ImGuiColorEditFlags_NoInputs);
        GUI::RenderCustomSwitch("Show Bg##FO",  &FPSOverlay::g_showBackground);
        if (FPSOverlay::g_showBackground)
            ImGui::SliderFloat("Opacity##FO", &FPSOverlay::g_bgOpacity, 0.0f, 1.0f, "%.2f");
        GUI::RenderCustomSwitch("Shadow##FO", &FPSOverlay::g_showShadow);
        ImGui::ColorEdit4("Accent##FO",  (float*)&FPSOverlay::g_accentColor, ImGuiColorEditFlags_NoInputs);
    } else if (strcmp(name, "Ping Counter") == 0) {
        ImGui::SliderFloat("Scale##PC", &PingCounter::g_pingTextScale, 0.5f, 3.0f, "%.2f");
        GUI::RenderCustomSwitch("Show Bg##PC", &PingCounter::g_showBackground);
        if (PingCounter::g_showBackground)
            ImGui::SliderFloat("Opacity##PC", &PingCounter::g_bgOpacity, 0.0f, 1.0f, "%.2f");
        ImGui::ColorEdit4("Color##PC", (float*)&PingCounter::g_pingTextColor, ImGuiColorEditFlags_NoInputs);
    } else if (strcmp(name, "Player Info") == 0) {
        ImGui::Text("Name: %s", PlayerInfo::g_playerName.empty() ? "Not found" : PlayerInfo::g_playerName.c_str());
        if (PlayerInfo::g_skinTexture) {
            ImGui::Text("Skin: loaded (%dx%d)", PlayerInfo::g_texWidth, PlayerInfo::g_texHeight);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Skin not found (custom.png)");
        }
        if (ImGui::Button("Reload Data##PI")) {
            PlayerInfo::RefreshPlayerData();
        }
        ImGui::Separator();
        ImGui::SliderFloat("Head Size##PI", &PlayerInfo::g_headSize, 18.0f, 72.0f, "%.0f");
        ImGui::SliderFloat("Text Scale##PI", &PlayerInfo::g_textScale, 0.5f, 3.0f, "%.2f");
        GUI::RenderCustomSwitch("Show Hat Layer##PI", &PlayerInfo::g_showHatLayer);
        GUI::RenderCustomSwitch("Rounded Head##PI", &PlayerInfo::g_headRounded);
        if (PlayerInfo::g_headRounded) {
            ImGui::SliderFloat("Head Radius##PI", &PlayerInfo::g_headRadius, 0.0f, 32.0f, "%.0f");
        }
        GUI::RenderCustomSwitch("Show Background##PI", &PlayerInfo::g_showBackground);
        if (PlayerInfo::g_showBackground) {
            ImGui::SliderFloat("Bg Opacity##PI", &PlayerInfo::g_bgOpacity, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Bg Radius##PI", &PlayerInfo::g_bgRadius, 0.0f, 24.0f, "%.0f");
        }
        GUI::RenderCustomSwitch("Show Border##PI", &PlayerInfo::g_showBorder);
        if (PlayerInfo::g_showBorder) {
            ImGui::ColorEdit4("Border Color##PI", (float*)&PlayerInfo::g_borderColor, ImGuiColorEditFlags_NoInputs);
        }
        ImGui::ColorEdit4("Name Color##PI", (float*)&PlayerInfo::g_nameColor, ImGuiColorEditFlags_NoInputs);
        ImGui::TextDisabled("Drag the HUD box in-game to reposition it.");
    } else if (strcmp(name, "ClickGUI") == 0) {
        static const char* st[] = {"Regular","Separated","Rise","Lunar"};
        ImGui::Combo("Style##CG", &ClickGUI::g_guiStyle, st, 4);
        static const char* bg[] = {"Normal Dark","Mica Blur"};
        ImGui::Combo("Background##CG", &ClickGUI::g_bgStyle, bg, 2);
        if (ClickGUI::g_bgStyle == 1) {
            ImGui::SliderFloat("Blur Radius##CG2",  &ClickGUI::g_blurRadius, 1.0f, 12.0f, "%.1f");
            ImGui::SliderFloat("Blur Opacity##CG2", &ClickGUI::g_blurOpacity, 0.0f, 1.0f, "%.2f");
        }
        GUI::RenderCustomSwitch("Particles##CG", &ClickGUI::g_showParticles);
        const char* th[] = {"Aegle Classic","Sakura Blossom","Cyberpunk 2077","Emerald Forest","Deep Sea"};
        int ct = GUI::g_currentTheme;
        if (ImGui::Combo("Theme##CG", &ct, th, 5)) GUI::ApplyThemePreset(ct);
    } else if (strcmp(name, "AutoClicker") == 0) {
        ImGui::SliderFloat("CPS##AC",          &AutoClicker::g_cps,         1.0f, 30.0f, "%.1f");
        ImGui::SliderFloat("Random Range##AC", &AutoClicker::g_randomRange, 0.0f,  8.0f, "%.1f");
        GUI::RenderCustomSwitch("Right Click##AC", &AutoClicker::g_rightClick);
        GUI::RenderCustomSwitch("Hold Mode##AC",   &AutoClicker::g_holdMode);
    } else if (strcmp(name, "Anti-AFK") == 0) {
        ImGui::SliderFloat("Interval (s)##AFK",  &AntiAFK::g_intervalSecs,    5.0f, 120.0f, "%.0f s");
        ImGui::SliderFloat("Duration (ms)##AFK",  &AntiAFK::g_pressDurationMs, 50.0f, 500.0f, "%.0f ms");
        GUI::RenderCustomSwitch("Randomize Keys##AFK", &AntiAFK::g_randomizeKeys);
        GUI::RenderCustomSwitch("Jump##AFK",             &AntiAFK::g_jump);
    } else if (strcmp(name, "Screenshot") == 0) {
        static const char* fkeys[] = {
            "F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"
        };
        int fkIdx = Screenshot::g_hotkey - VK_F1;
        if (fkIdx < 0)  fkIdx = 0;
        if (fkIdx > 11) fkIdx = 11;
        if (ImGui::Combo("Hotkey##SS", &fkIdx, fkeys, 12))
            Screenshot::g_hotkey = VK_F1 + fkIdx;
        GUI::RenderCustomSwitch("Notify##SS",    &Screenshot::g_notifyOnCapture);
        GUI::RenderCustomSwitch("Show on HUD##SS", &Screenshot::g_showHud);
    }
}

// ──────────────────────────────────────────────
// Rise style rendering
// ──────────────────────────────────────────────

static void DrawTabIcon(ImDrawList* draw, ImVec2 pos, int index, bool active, float sc, ImU32 color) {
    // Draw using textures if loaded
    if (index == 1 && GUI::g_tabTextures[0]) { // Combat
        draw->AddImage(GUI::g_tabTextures[0], pos, pos + ImVec2(18, 18) * sc, ImVec2(0,0), ImVec2(1,1), color);
        return;
    }
    if (index == 2 && GUI::g_tabTextures[1]) { // Movement
        draw->AddImage(GUI::g_tabTextures[1], pos, pos + ImVec2(18, 18) * sc, ImVec2(0,0), ImVec2(1,1), color);
        return;
    }
    if (index == 3 && GUI::g_tabTextures[2]) { // Render (Visuals)
        draw->AddImage(GUI::g_tabTextures[2], pos, pos + ImVec2(18, 18) * sc, ImVec2(0,0), ImVec2(1,1), color);
        return;
    }
    if (index == 4 && GUI::g_tabTextures[3]) { // Exploit (Misc)
        draw->AddImage(GUI::g_tabTextures[3], pos, pos + ImVec2(18, 18) * sc, ImVec2(0,0), ImVec2(1,1), color);
        return;
    }
    if (index == 7 && GUI::g_tabTextures[7]) { // Config Market
        draw->AddImage(GUI::g_tabTextures[7], pos, pos + ImVec2(18, 18) * sc, ImVec2(0,0), ImVec2(1,1), color);
        return;
    }
    
    // Fallbacks and custom vectors
    if (index == 0) { // Search
        draw->AddCircle(pos + ImVec2(6, 6) * sc, 4.5f * sc, color, 16, 1.5f * sc);
        draw->AddLine(pos + ImVec2(9, 9) * sc, pos + ImVec2(14, 14) * sc, color, 1.8f * sc);
    } else if (index == 5) { // Terminal (>_)
        draw->AddLine(pos + ImVec2(4, 4) * sc, pos + ImVec2(8, 8) * sc, color, 1.5f * sc);
        draw->AddLine(pos + ImVec2(8, 8) * sc, pos + ImVec2(4, 12) * sc, color, 1.5f * sc);
        draw->AddLine(pos + ImVec2(10, 12) * sc, pos + ImVec2(16, 12) * sc, color, 1.5f * sc);
    } else if (index == 6) { // IRC Chat (Speech bubble)
        draw->AddCircle(pos + ImVec2(9, 8) * sc, 5.5f * sc, color, 16, 1.5f * sc);
        draw->AddTriangleFilled(pos + ImVec2(6, 12) * sc, pos + ImVec2(4, 15) * sc, pos + ImVec2(9, 13) * sc, color);
    } else if (index == 8) { // Themes (Paint palette)
        draw->AddCircle(pos + ImVec2(9, 9) * sc, 6.5f * sc, color, 16, 1.5f * sc);
        draw->AddCircleFilled(pos + ImVec2(6, 7) * sc, 1.2f * sc, ImColor(255, 80, 80));
        draw->AddCircleFilled(pos + ImVec2(12, 7) * sc, 1.2f * sc, ImColor(80, 255, 80));
        draw->AddCircleFilled(pos + ImVec2(9, 12) * sc, 1.2f * sc, ImColor(80, 80, 255));
    } else if (index == 9) { // Settings (Gear)
        draw->AddCircle(pos + ImVec2(9, 9) * sc, 4.5f * sc, color, 12, 1.5f * sc);
        draw->AddCircleFilled(pos + ImVec2(9, 9) * sc, 2.0f * sc, color);
        // Draw gear teeth/spokes
        for (int a = 0; a < 8; a++) {
            float rad = a * 3.14159265f / 4.0f;
            ImVec2 inner(9.0f + cosf(rad) * 4.5f, 9.0f + sinf(rad) * 4.5f);
            ImVec2 outer(9.0f + cosf(rad) * 7.0f, 9.0f + sinf(rad) * 7.0f);
            draw->AddLine(pos + inner * sc, pos + outer * sc, color, 1.5f * sc);
        }
    }
}

static void RenderRiseModulesList(const char* query, const char* categoryFilter, float sc) {
    // Structure of module entry
    struct ModuleEntry {
        std::string name;
        std::string category;
        std::string description;
        bool* enabled;
        void (*callback)();
    };
    
    struct LocalToggles {
        static void toggleReach()     { Reach::SetEnabled(Reach::g_reachEnabled); }
        static void toggleHitbox()    { if (Hitbox::g_hitboxEnabled) Hitbox::Enable(); else Hitbox::Disable(); }
        static void toggleTimer()     { if (Timer::g_timerEnabled)   Timer::Enable(); else   Timer::Disable(); }
        static void toggleFullBright(){ if (FullBright::g_fullBrightEnabled) FullBright::Enable(); else FullBright::Disable(); }
        static void toggleFPSOverlay() {
            if (FPSOverlay::g_showFpsOverlay) {
                FPSOverlay::g_fpsOverlayEnableTime  = GetTickCount64();
                FPSOverlay::g_fpsOverlayDisableTime = 0;
            } else {
                FPSOverlay::g_fpsOverlayDisableTime = GetTickCount64();
                FPSOverlay::g_fpsOverlayEnableTime  = 0;
            }
        }
        static void toggleClickGUI() { g_showMenu = ClickGUI::g_enabled; GUI::g_showMenu = g_showMenu; }
    };

    // Static vector of functional modules only (no mock modules)
    static std::vector<ModuleEntry> modules = {
        // Combat
        { "Reach", "Combat", "Extends your attack reach / range on servers.", &Reach::g_reachEnabled, LocalToggles::toggleReach },
        { "Hitbox", "Combat", "Expands client-side player hitboxes for easier hits.", &Hitbox::g_hitboxEnabled, LocalToggles::toggleHitbox },
        
        // Movement
        { "AutoSprint", "Movement", "Automatically sprints without pressing the sprint key.", &AutoSprint::g_autoSprintEnabled, nullptr },
        { "Timer", "Movement", "Accelerates or decelerates the game's internal speed.", &Timer::g_timerEnabled, LocalToggles::toggleTimer },
        
        // Render
        { "Watermark", "Render", "Renders the aesthetic Aegleseeker watermark overlay.", &Watermark::g_showWatermark, nullptr },
        { "ArrayList", "Render", "Displays active client modules in a clean list.", &ArrayList::g_enabled, nullptr },
        { "Render Info", "Render", "Shows useful stats (FPS, Ping, coordinates, etc.).", &RenderInfo::g_showRenderInfo, nullptr },
        { "Keystrokes", "Render", "Displays WASD and mouse buttons pressed on screen.", &Keystrokes::g_showKeystrokes, nullptr },
        { "CPS Counter", "Render", "Renders the Clicks-Per-Second indicator hud.", &CPSCounter::g_showCpsCounter, nullptr },
        { "FPS Overlay", "Render", "Shows frames-per-second count with optional graphs.", &FPSOverlay::g_showFpsOverlay, LocalToggles::toggleFPSOverlay },
        { "Ping Counter", "Render", "Shows network latency/ping on HUD.", &PingCounter::g_showPingCounter, nullptr },
        { "Player Info", "Render", "Displays your skin head and nickname on HUD.", &PlayerInfo::g_showPlayerInfo, nullptr },
        { "FullBright", "Render", "Forces light levels to maximum brightness.", &FullBright::g_fullBrightEnabled, LocalToggles::toggleFullBright },
        { "MotionBlur", "Render", "Adds a realistic screen motion blur effect.", &MotionBlur::g_motionBlurEnabled, nullptr },
        { "ClickGUI", "Render", "Toggles and configures this ClickGUI overlay.", &ClickGUI::g_enabled, LocalToggles::toggleClickGUI },
        
        // Exploit
        { "UnlockFPS", "Exploit", "Removes default FPS caps to run at maximum refresh.", &UnlockFPS::g_unlockFpsEnabled, nullptr },
        { "AutoClicker", "Exploit", "Automatically clicks your mouse at a configurable speed.", &AutoClicker::g_enabled, nullptr },
        { "Anti-AFK", "Exploit", "Performs background actions to prevent idle disconnects.", &AntiAFK::g_enabled, nullptr },
        { "Screenshot", "Exploit", "Takes a screenshot of the game using the set hotkey.", &Screenshot::g_enabled, nullptr }
    };
    
    static std::map<std::string, bool> expandedCards;
    static std::map<std::string, float> expandAnim;
    static std::map<std::string, float> measuredH;
    
    float availWidth = ImGui::GetContentRegionAvail().x - 15.0f;
    float dt = ImGui::GetIO().DeltaTime;
    
    // Filter functions
    auto matchesQuery = [](const std::string& text, const std::string& q) -> bool {
        if (q.empty()) return true;
        std::string textLower = text;
        for (auto& c : textLower) c = tolower(c);
        std::string qLower = q;
        for (auto& c : qLower) c = tolower(c);
        return textLower.find(qLower) != std::string::npos;
    };
    
    // Get colors matching selected theme
    ImVec4 accentV = GUI::g_colorAccent;
    ImU32 accentCol = ImGui::ColorConvertFloat4ToU32(accentV);
    
    for (auto& module : modules) {
        // Category filtering
        if (strcmp(categoryFilter, "All") != 0) {
            if (module.category != categoryFilter) continue;
        }
        
        // Search query filtering
        if (query && strlen(query) > 0) {
            if (!matchesQuery(module.name, query) && !matchesQuery(module.category, query))
                continue;
        }
        
        ImGui::PushID(module.name.c_str());
        ImVec2 startPos = ImGui::GetCursorScreenPos();
        
        bool hasSettings = ModuleHasSettings(module.name.c_str());
        bool expanded = hasSettings && expandedCards[module.name];
        
        // Smooth expand/collapse (frame-rate independent), height + fade together
        float ea = Animations::Approach(expandAnim[module.name], expanded ? 1.0f : 0.0f, dt, 9.0f);
        expandAnim[module.name] = ea;
        float ease = Animations::EaseOutQuart(Animations::Clamp01(ea));
        
        // Settings panel target height. The per-module caps keep big settings
        // panels (e.g. Keystrokes) scrollable; the measured natural height keeps
        // small panels from wasting space.
        float capH = 80.0f * sc; // small slider/switch modules
        if (expanded || ea > 0.001f) {
            if (strcmp(module.name.c_str(), "Keystrokes") == 0)   capH = 400.0f * sc;
            else if (strcmp(module.name.c_str(), "ArrayList") == 0)    capH = 200.0f * sc;
            else if (strcmp(module.name.c_str(), "FPS Overlay") == 0)  capH = 150.0f * sc;
            else if (strcmp(module.name.c_str(), "ClickGUI") == 0)     capH = 150.0f * sc;
            else if (strcmp(module.name.c_str(), "CPS Counter") == 0)  capH = 110.0f * sc;
            else if (strcmp(module.name.c_str(), "Render Info") == 0)  capH = 110.0f * sc;
            else if (strcmp(module.name.c_str(), "Ping Counter") == 0) capH = 110.0f * sc;
            else if (strcmp(module.name.c_str(), "AutoClicker") == 0)  capH = 135.0f * sc;
            else if (strcmp(module.name.c_str(), "Anti-AFK") == 0)     capH = 135.0f * sc;
            else if (strcmp(module.name.c_str(), "Screenshot") == 0)   capH = 110.0f * sc;
            float natH = measuredH[module.name];
            if (natH <= 0.0f) natH = capH;
            if (natH < capH) capH = natH;
        }
        float settingsH = capH * ease;
        float headerHeight = 70.0f * sc;
        float cardHeight   = headerHeight + settingsH + (settingsH > 0.0f ? 12.0f * sc : 0.0f);
        
        // === Header interaction area only — does NOT cover expanded settings ===
        ImGui::InvisibleButton("##card_header", ImVec2(availWidth, headerHeight));
        bool hovered      = ImGui::IsItemHovered();
        bool leftClicked  = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        
        if (leftClicked) {
            *module.enabled = !*module.enabled;
            if (module.callback) module.callback();
        }
        if (rightClicked && hasSettings) {
            expandedCards[module.name] = !expandedCards[module.name];
        }
        
        // Draw card background (full card height)
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImU32 bgCol = hovered ? ImColor(22, 22, 30, 200) : ImColor(14, 14, 18, 140);
        ImU32 borderCol = *module.enabled ? (ImU32)ImColor(accentV.x, accentV.y, accentV.z, 0.45f) : (ImU32)ImColor(45, 45, 55, hovered ? 120 : 60);
        
        draw->AddRectFilled(startPos, startPos + ImVec2(availWidth, cardHeight), bgCol, 12.0f * sc);
        draw->AddRect(startPos, startPos + ImVec2(availWidth, cardHeight), borderCol, 12.0f * sc, 0, 1.2f * sc);
        
        // Left border stripe for enabled modules
        if (*module.enabled) {
            draw->AddRectFilled(startPos, startPos + ImVec2(4.0f * sc, cardHeight), accentCol, 12.0f * sc, ImDrawFlags_RoundCornersLeft);
        }
        
        // Module name
        ImGui::PushFont(GUI::g_fontH3 ? GUI::g_fontH3 : ImGui::GetFont());
        ImU32 nameColor = *module.enabled ? accentCol : (ImU32)ImColor(240, 240, 245, 255);
        draw->AddText(startPos + ImVec2(18.0f * sc, 12.0f * sc), nameColor, module.name.c_str());
        ImGui::PopFont();
        
        // Category label + description
        ImGui::PushFont(GUI::g_fontDefault);
        ImVec2 titleSize = ImGui::CalcTextSize(module.name.c_str());
        std::string catStr = " (" + module.category + ")";
        draw->AddText(startPos + ImVec2(18.0f * sc + titleSize.x * 1.1f + 5.0f * sc, 14.0f * sc), ImColor(120, 120, 130, 200), catStr.c_str());
        draw->AddText(startPos + ImVec2(18.0f * sc, 38.0f * sc), ImColor(160, 160, 170, 220), module.description.c_str());
        ImGui::PopFont();
        
        // Toggle switch
        float switchWidth  = 34.0f * sc;
        float switchHeight = 18.0f * sc;
        ImVec2 switchPos   = startPos + ImVec2(availWidth - switchWidth - 20.0f * sc, 20.0f * sc);
        ImU32 switchBg     = *module.enabled ? accentCol : (ImU32)ImColor(45, 45, 52, 255);
        draw->AddRectFilled(switchPos, switchPos + ImVec2(switchWidth, switchHeight), switchBg, 9.0f * sc);
        draw->AddRect(switchPos, switchPos + ImVec2(switchWidth, switchHeight), ImColor(80, 80, 90, 150), 9.0f * sc, 0, 1.0f * sc);
        float circleRadius = 6.5f * sc;
        float circleX = *module.enabled ? (switchPos.x + switchWidth - circleRadius - 3.0f * sc) : (switchPos.x + circleRadius + 3.0f * sc);
        draw->AddCircleFilled(ImVec2(circleX, switchPos.y + switchHeight * 0.5f), circleRadius, ImColor(255, 255, 255, 255));
        
        // === Expanded settings via BeginChild — cursor advances correctly, widgets don't leak ===
        if (ea > 0.01f) {
            // Fade + height grow together; scrollbar fades in so it never flashes
            // while the panel expands.
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ease);
            ImGui::SetCursorScreenPos(startPos + ImVec2(8.0f * sc, headerHeight + 6.0f * sc));
            
            std::string childId = std::string("##cfg_") + module.name;
            ImGui::PushStyleColor(ImGuiCol_ChildBg,      ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,  ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.5f, 0.5f, 0.55f, 0.55f * ease));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * sc, 6.0f * sc));
            
            // BeginChild with explicit animated height — scrollbar appears automatically when content overflows
            bool childOk = ImGui::BeginChild(childId.c_str(),
                ImVec2(availWidth - 16.0f * sc, settingsH),
                false,
                ImGuiWindowFlags_None);
            if (childOk) {
                ImGui::PushItemWidth(availWidth - 56.0f * sc);
                ClickGUI::RenderModuleSettings(module.name.c_str(), availWidth - 56.0f * sc);
                ImGui::PopItemWidth();
                measuredH[module.name] = ImGui::GetCursorPosY() + 8.0f;
            }
            ImGui::EndChild();  // After EndChild, parent cursor is correctly positioned below the child
            
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(); // alpha
        }
        
        ImGui::PopID();
        ImGui::Spacing();
    }
}

void ClickGUI::RenderRiseMenu(float screenWidth, float screenHeight) {
    float e = Animations::EaseOutQuart(GUI::g_menuAnim);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, e);
    
    // Background blits
    if (g_bgStyle == 0) {
        ImU32 bgCol = IM_COL32(5, 5, 10, (int)(e * 180.0f));
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(screenWidth, screenHeight), bgCol);
    } else {
        ImU32 tint = IM_COL32(5, 5, 10, (int)(e * 60.0f));
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(screenWidth, screenHeight), tint);
    }
    
    if (g_showParticles) {
        GUI::RenderParticles(ImGui::GetBackgroundDrawList(), ImVec2(0, 0), ImVec2(screenWidth, screenHeight), e);
    }
    
    // Scale animation
    float sc = 0.94f + (0.06f * e);
    ImVec2 baseSize = ImVec2(900, 600);
    ImVec2 winSize = ImVec2(baseSize.x * sc, baseSize.y * sc);
    ImVec2 winPos = ImVec2(screenWidth / 2 - winSize.x / 2, screenHeight / 2 - winSize.y / 2);
    
    ImGui::SetNextWindowSize(winSize, ImGuiCond_Always);
    ImGui::SetNextWindowPos(winPos, ImGuiCond_Always);
    
    // Draw shadow
    GUI::DrawShadow(ImGui::GetBackgroundDrawList(), winPos, winSize, 24.0f * sc, 30.0f * e, 0.45f * e);
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 24.0f * sc);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.07f, 0.92f));
    
    if (ImGui::Begin("RiseClickGUIWindow", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
        ImVec2 wPos = ImGui::GetWindowPos();
        ImVec2 wSize = ImGui::GetWindowSize();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        
        // Theme color accent matching
        ImVec4 accentV = GUI::g_colorAccent;
        ImU32 accentCol = ImGui::ColorConvertFloat4ToU32(accentV);
        
        // Vertical separator line
        draw->AddLine(
            ImVec2(wPos.x + 210.0f * sc, wPos.y),
            ImVec2(wPos.x + 210.0f * sc, wPos.y + wSize.y),
            ImColor(35, 35, 45, 80),
            1.5f
        );
        
        // Render Rise 6.0 Title at top left (matches selected theme accent color for "6.0")
        ImGui::PushFont(GUI::g_fontH1 ? GUI::g_fontH1 : ImGui::GetFont());
        ImVec2 riseSize = ImGui::CalcTextSize("Aegleseeker");
        draw->AddText(wPos + ImVec2(30.0f * sc, 25.0f * sc), ImColor(240, 240, 245), "Aegleseeker");
        ImGui::PopFont();
        
        ImGui::PushFont(GUI::g_fontDefault);
        draw->AddText(wPos + ImVec2(30.0f * sc + riseSize.x + 4.0f * sc, 25.0f * sc + 4.0f * sc), accentCol, "1.0.8");
        ImGui::PopFont();
        
        // Sidebar Navigation (10 tabs)
        const char* tabs[] = { "Search", "Combat", "Movement", "Render", "Exploit", "Terminal", "IRC Chat", "Config Market", "Themes", "Settings" };
        static int currentTab = 0;
        if (currentTab >= 10) currentTab = 0; // boundary check
        
        // Sliding indicator pill (uses theme accent color)
        static float riseIndicatorY = 85.0f * sc;
        static float riseTargetIndicatorY = 85.0f * sc;
        float targetY = 85.0f * sc + currentTab * 42.0f * sc;
        riseTargetIndicatorY = targetY;
        
        float dt = ImGui::GetIO().DeltaTime;
        riseIndicatorY = Animations::Approach(riseIndicatorY, riseTargetIndicatorY, dt, 14.0f);
        
        // Draw active indicator pill (using accentCol with custom transparency)
        draw->AddRectFilled(wPos + ImVec2(15.0f * sc, riseIndicatorY), wPos + ImVec2(195.0f * sc, riseIndicatorY + 36.0f * sc), ImColor(accentV.x, accentV.y, accentV.z, 0.75f), 8.0f * sc);
        
        // Draw tabs menu buttons
        for (int i = 0; i < 10; i++) {
            float tabY = 85.0f * sc + i * 42.0f * sc;
            ImGui::SetCursorPos(ImVec2(15.0f * sc, tabY));
            
            std::string btnId = "##tab_btn_" + std::to_string(i);
            ImGui::InvisibleButton(btnId.c_str(), ImVec2(180.0f * sc, 36.0f * sc));
            
            bool hovered = ImGui::IsItemHovered();
            bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            
            if (clicked) {
                currentTab = i;
                
                // Map to GUI::g_currentTab for animations/state (like IRC Chat sidebar)
                if (i == 1) GUI::g_currentTab = 0;
                else if (i == 2) GUI::g_currentTab = 1;
                else if (i == 3) GUI::g_currentTab = 2;
                else if (i == 4) GUI::g_currentTab = 3;
                else if (i == 5) GUI::g_currentTab = 4;
                else if (i == 6) GUI::g_currentTab = 6;
                else if (i == 7) GUI::g_currentTab = 7;
                else GUI::g_currentTab = -1;
            }
            
            if (hovered && currentTab != i) {
                draw->AddRectFilled(wPos + ImVec2(15.0f * sc, tabY), wPos + ImVec2(195.0f * sc, tabY + 36.0f * sc), ImColor(255, 255, 255, 12), 8.0f * sc);
            }
            
            ImVec2 iconPos = wPos + ImVec2(28.0f * sc, tabY + 9.0f * sc);
            ImU32 textColor = (currentTab == i) ? ImColor(255, 255, 255, 255) : ImColor(170, 170, 180, 220);
            
            DrawTabIcon(draw, iconPos, i, currentTab == i, sc, textColor);
            
            ImGui::PushFont(GUI::g_fontDefault);
            draw->AddText(wPos + ImVec2(58.0f * sc, tabY + 8.0f * sc), textColor, tabs[i]);
            ImGui::PopFont();
        }
        
        // Content Area child
        ImGui::SetCursorPos(ImVec2(225.0f * sc, 20.0f * sc));
        ImGui::BeginChild("RiseContentArea", ImVec2(winSize.x - 245.0f * sc, winSize.y - 40.0f * sc), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_None);
        {
            ImVec2 contentPos = ImGui::GetWindowPos();
            ImVec2 contentSize = ImGui::GetWindowSize();
            ImDrawList* cDraw = ImGui::GetWindowDrawList();
            
            static char searchBarText[128] = "";
            
            if (currentTab == 0) { // Search Tab
                ImGui::SetCursorPos(ImVec2(10.0f * sc, 10.0f * sc));
                
                // Draw custom magnifying glass icon next to search text
                ImVec2 searchIconPos = contentPos + ImVec2(15.0f * sc, 18.0f * sc);
                cDraw->AddCircle(searchIconPos + ImVec2(5, 5) * sc, 4.0f * sc, ImColor(140, 140, 150), 16, 1.2f * sc);
                cDraw->AddLine(searchIconPos + ImVec2(8, 8) * sc, searchIconPos + ImVec2(12, 12) * sc, ImColor(140, 140, 150), 1.5f * sc);
                
                ImGui::SetCursorPos(ImVec2(35.0f * sc, 12.0f * sc));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.98f, 1.0f));
                
                ImGui::PushFont(GUI::g_fontDefault);
                ImGui::InputTextWithHint("##RiseSearchInput", "Start typing to search...", searchBarText, IM_ARRAYSIZE(searchBarText));
                ImGui::PopFont();
                ImGui::PopStyleColor(4);
                
                // Scrollable list below
                ImGui::SetCursorPos(ImVec2(10.0f * sc, 48.0f * sc));
                ImGui::BeginChild("RiseSearchScroll", ImVec2(contentSize.x - 20.0f * sc, contentSize.y - 58.0f * sc), false, ImGuiWindowFlags_None);
                {
                    RenderRiseModulesList(searchBarText, "All", sc);
                }
                ImGui::EndChild();
                
            } else if (currentTab >= 1 && currentTab <= 4) { // Modules tabs
                const char* categoryMap[] = { "", "Combat", "Movement", "Render", "Exploit" };
                const char* activeCategory = categoryMap[currentTab];
                
                // Header for Category
                ImGui::SetCursorPos(ImVec2(15.0f * sc, 10.0f * sc));
                ImGui::PushFont(GUI::g_fontH2 ? GUI::g_fontH2 : ImGui::GetFont());
                ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.98f, 1.0f), activeCategory);
                ImGui::PopFont();
                
                // Scrollable list below
                ImGui::SetCursorPos(ImVec2(10.0f * sc, 45.0f * sc));
                ImGui::BeginChild("RiseCategoryScroll", ImVec2(contentSize.x - 20.0f * sc, contentSize.y - 55.0f * sc), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_None);
                {
                    RenderRiseModulesList("", activeCategory, sc);
                }
                ImGui::EndChild();
                
            } else if (currentTab == 5) { // Terminal Tab
                ImGui::SetCursorPos(ImVec2(10.0f * sc, 10.0f * sc));
                ImGui::BeginChild("RiseTerminal", ImVec2(contentSize.x - 20.0f * sc, contentSize.y - 20.0f * sc), false, ImGuiWindowFlags_None);
                {
                    Terminal::RenderConsole();
                }
                ImGui::EndChild();
                
            } else if (currentTab == 6) { // IRC Chat Tab
                ImGui::SetCursorPos(ImVec2(10.0f * sc, 10.0f * sc));
                ImGui::BeginChild("RiseIRCChat", ImVec2(contentSize.x - 20.0f * sc, contentSize.y - 20.0f * sc), false, ImGuiWindowFlags_None);
                {
                    IRChat::RenderMenu();
                }
                ImGui::EndChild();
                
            } else if (currentTab == 7) { // Config Market Tab
                ImGui::SetCursorPos(ImVec2(10.0f * sc, 10.0f * sc));
                ImGui::BeginChild("RiseConfigMarket", ImVec2(contentSize.x - 20.0f * sc, contentSize.y - 20.0f * sc), false, ImGuiWindowFlags_None);
                {
                    GUI::RenderConfigMarket();
                }
                ImGui::EndChild();
                
            } else if (currentTab == 8) { // Themes Tab
                ImGui::SetCursorPos(ImVec2(15.0f * sc, 15.0f * sc));
                ImGui::PushFont(GUI::g_fontH2 ? GUI::g_fontH2 : ImGui::GetFont());
                ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.98f, 1.0f), "Theme Presets");
                ImGui::PopFont();
                
                ImGui::SetCursorPos(ImVec2(10.0f * sc, 55.0f * sc));
                ImGui::BeginChild("RiseThemesScroll", ImVec2(contentSize.x - 20.0f * sc, contentSize.y - 65.0f * sc), false, ImGuiWindowFlags_None);
                {
                    const char* themeNames[] = { "Aegle Classic", "Sakura Blossom", "Cyberpunk 2077", "Emerald Forest", "Deep Sea" };
                    const char* themeDescs[] = {
                        "The iconic dark theme with pink and magenta accents.",
                        "A soft, pleasant cherry blossom theme with pink gradients.",
                        "High-contrast cyberpunk neon yellow and dark gray theme.",
                        "Calming green theme reminiscent of forest canopies.",
                        "Rich deep ocean theme with blue and teal highlights."
                    };
                    
                    float themeWidth = ImGui::GetContentRegionAvail().x - 15.0f;
                    for (int t = 0; t < 5; t++) {
                        ImGui::PushID(t);
                        ImVec2 tStart = ImGui::GetCursorScreenPos();
                        bool activeTheme = (GUI::g_currentTheme == t);
                        
                        ImGui::InvisibleButton("##theme_card", ImVec2(themeWidth, 65.0f * sc));
                        bool hovered = ImGui::IsItemHovered();
                        if (ImGui::IsItemClicked()) {
                            GUI::ApplyThemePreset(t);
                        }
                        
                        ImDrawList* tDraw = ImGui::GetWindowDrawList();
                        ImU32 themeBg = hovered ? ImColor(24, 24, 32, 180) : ImColor(16, 16, 22, 130);
                        tDraw->AddRectFilled(tStart, tStart + ImVec2(themeWidth, 65.0f * sc), themeBg, 10.0f * sc);
                        
                        ImU32 themeBorder = activeTheme ? accentCol : (ImU32)ImColor(45, 45, 55, hovered ? 120 : 60);
                        tDraw->AddRect(tStart, tStart + ImVec2(themeWidth, 65.0f * sc), themeBorder, 10.0f * sc, 0, 1.2f * sc);
                        
                        ImGui::PushFont(GUI::g_fontH3 ? GUI::g_fontH3 : ImGui::GetFont());
                        tDraw->AddText(tStart + ImVec2(15.0f * sc, 10.0f * sc), activeTheme ? accentCol : (ImU32)ImColor(240, 240, 245), themeNames[t]);
                        ImGui::PopFont();
                        
                        ImGui::PushFont(GUI::g_fontDefault);
                        tDraw->AddText(tStart + ImVec2(15.0f * sc, 35.0f * sc), ImColor(150, 150, 160), themeDescs[t]);
                        ImGui::PopFont();
                        
                        ImGui::PopID();
                        ImGui::Spacing();
                    }
                }
                ImGui::EndChild();
                
            } else if (currentTab == 9) { // ClickGUI Config Settings Tab
                ImGui::SetCursorPos(ImVec2(15.0f * sc, 15.0f * sc));
                ImGui::PushFont(GUI::g_fontH2 ? GUI::g_fontH2 : ImGui::GetFont());
                ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.98f, 1.0f), "ClickGUI Configuration");
                ImGui::PopFont();
                
                ImGui::SetCursorPos(ImVec2(10.0f * sc, 55.0f * sc));
                ImGui::BeginChild("RiseLangScroll", ImVec2(contentSize.x - 20.0f * sc, contentSize.y - 65.0f * sc), false, ImGuiWindowFlags_None);
                {
                    ImGui::PushFont(GUI::g_fontH3 ? GUI::g_fontH3 : ImGui::GetFont());
                    ImGui::TextColored(GUI::g_colorAccent, "Background & Rendering");
                    ImGui::PopFont();
                    ImGui::Separator();
                    ImGui::Spacing();
                    
                    const char* bgStyles[] = { "Normal Dark", "Mica Blur" };
                    ImGui::Combo("Background Style##RiseSet", &ClickGUI::g_bgStyle, bgStyles, IM_ARRAYSIZE(bgStyles));
                    
                    if (ClickGUI::g_bgStyle == 1) {
                        ImGui::SliderFloat("Blur Radius##RiseSet", &ClickGUI::g_blurRadius, 1.0f, 12.0f, "%.1f");
                        ImGui::SliderFloat("Blur Opacity##RiseSet", &ClickGUI::g_blurOpacity, 0.0f, 1.0f, "%.2f");
                    }
                    
                    GUI::RenderCustomSwitch("Plexus Particles##RiseSet", &ClickGUI::g_showParticles);
                    
                    ImGui::Spacing(); ImGui::Spacing();
                    
                    ImGui::PushFont(GUI::g_fontH3 ? GUI::g_fontH3 : ImGui::GetFont());
                    ImGui::TextColored(GUI::g_colorAccent, "Client Information");
                    ImGui::PopFont();
                    ImGui::Separator();
                    ImGui::Spacing();
                    
                    ImGui::Text("Active Theme: %s", (GUI::g_currentTheme == 0) ? "Aegle Classic" :
                                                    (GUI::g_currentTheme == 1) ? "Sakura Blossom" :
                                                    (GUI::g_currentTheme == 2) ? "Cyberpunk 2077" :
                                                    (GUI::g_currentTheme == 3) ? "Emerald Forest" : "Deep Sea");
                    ImGui::Text("Client Version: Aegleseeker v1.0.8");
                }
                ImGui::EndChild();
            }
        }
        ImGui::EndChild();
        ImGui::End();
    }
    
    // IRC Config Sidebar window (rendered outside the main window, matching GUI.cpp behavior)
    if (GUI::g_ircShiftAnim > 0.001f) {
        ImVec4 accentV = GUI::g_colorAccent;
        ImU32 accentCol = ImGui::ColorConvertFloat4ToU32(accentV);
        float sidebarAlpha = GUI::g_ircShiftAnim * e;
        float sidebarWidth = 220.0f * sc * Animations::EaseOutQuart(GUI::g_ircShiftAnim);
        float sidebarX = winPos.x + winSize.x + 15.0f * sc;
        
        ImGui::SetNextWindowSize(ImVec2(sidebarWidth, winSize.y), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(sidebarX, winPos.y), ImGuiCond_Always);
        
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, sidebarAlpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f * sc);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * sc, 12.0f * sc));
        
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.06f, 0.09f, 0.98f));
        
        // Draw matching shadow
        GUI::DrawShadow(ImGui::GetBackgroundDrawList(), ImVec2(sidebarX, winPos.y), ImVec2(sidebarWidth, winSize.y), 12.0f * sc, 20.0f * GUI::g_ircShiftAnim, 0.35f * GUI::g_ircShiftAnim);
        
        if (ImGui::Begin("IRC Config Sidebar##Rise", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar)) {
            
            ImGui::PushFont(GUI::g_fontH3 ? GUI::g_fontH3 : ImGui::GetFont());
            ImGui::SetCursorPosY(15.0f * sc);
            if (sidebarWidth > 100.0f * sc) {
                float textWidth = ImGui::CalcTextSize("IRC Configs").x;
                ImGui::SetCursorPosX((sidebarWidth - textWidth) * 0.5f);
                ImGui::TextColored(accentV, "IRC Configs");
            }
            ImGui::PopFont();
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            if (sidebarWidth > 150.0f * sc) {
                if (ConfigManager::GetConfigDir().empty())
                    ConfigManager::Initialize();
                
                auto configs = ConfigManager::ListConfigs();
                if (configs.empty()) {
                    ImGui::TextDisabled("No configs found.");
                } else {
                    ImGui::TextDisabled("Drag to the chat:\n");
                    ImGui::Spacing();
                    
                    ImGui::BeginChild("SidebarConfigList##Rise", ImVec2(0, 0), false, ImGuiWindowFlags_None);
                    for (const auto& cfg : configs) {
                        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(accentV.x, accentV.y, accentV.z, 0.4f));
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(accentV.x, accentV.y, accentV.z, 0.2f));
                        
                        ImGui::Selectable(cfg.c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
                        
                        // Drag Drop Source
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                            ImGui::SetDragDropPayload("DRAG_IRC_CONFIG", cfg.c_str(), cfg.size() + 1);
                            ImGui::Text("Enviar %s.json", cfg.c_str());
                            ImGui::EndDragDropSource();
                        }
                        
                        ImGui::PopStyleColor(2);
                    }
                    ImGui::EndChild();
                }
            }
            ImGui::End();
        }
        
        ImGui::PopStyleColor(1);
        ImGui::PopStyleVar(4);
    }
    
    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(4);
}

// ──────────────────────────────────────────────
// Lunar style rendering
// ──────────────────────────────────────────────

static void RenderLunarModulesList(const char* categoryFilter, const char* query, float sc, float contentAnim) {
    struct LModule {
        const char* name;
        const char* icon;
        const char* category;
        const char* description;
        bool* enabled;
        void (*callback)();
    };
    struct LLocal {
        static void toggleReach()      { Reach::SetEnabled(Reach::g_reachEnabled); }
        static void toggleHitbox()     { if (Hitbox::g_hitboxEnabled) Hitbox::Enable(); else Hitbox::Disable(); }
        static void toggleTimer()      { if (Timer::g_timerEnabled)   Timer::Enable(); else   Timer::Disable(); }
        static void toggleFullBright() { if (FullBright::g_fullBrightEnabled) FullBright::Enable(); else FullBright::Disable(); }
        static void toggleFPSOverlay() {
            if (FPSOverlay::g_showFpsOverlay) {
                FPSOverlay::g_fpsOverlayEnableTime  = GetTickCount64();
                FPSOverlay::g_fpsOverlayDisableTime = 0;
            } else {
                FPSOverlay::g_fpsOverlayDisableTime = GetTickCount64();
                FPSOverlay::g_fpsOverlayEnableTime  = 0;
            }
        }
        static void toggleClickGUI()   { g_showMenu = ClickGUI::g_enabled; GUI::g_showMenu = g_showMenu; }
    };

    static const std::vector<LModule> modules = {
        // Combat
        { "Reach", "R", "Combat", "Extends your attack reach / range on servers.", &Reach::g_reachEnabled, LLocal::toggleReach },
        { "Hitbox", "H", "Combat", "Expands client-side player hitboxes for easier hits.", &Hitbox::g_hitboxEnabled, LLocal::toggleHitbox },
        // Movement
        { "AutoSprint", "A", "Movement", "Automatically sprints without pressing the sprint key.", &AutoSprint::g_autoSprintEnabled, nullptr },
        { "Timer", "T", "Movement", "Accelerates or decelerates the game's internal speed.", &Timer::g_timerEnabled, LLocal::toggleTimer },
        // Render
        { "Watermark", "W", "Render", "Renders the aesthetic Aegleseeker watermark overlay.", &Watermark::g_showWatermark, nullptr },
        { "ArrayList", "L", "Render", "Displays active client modules in a clean list.", &ArrayList::g_enabled, nullptr },
        { "Render Info", "I", "Render", "Shows useful stats (FPS, Ping, coordinates, etc.).", &RenderInfo::g_showRenderInfo, nullptr },
        { "Keystrokes", "K", "Render", "Displays WASD and mouse buttons pressed on screen.", &Keystrokes::g_showKeystrokes, nullptr },
        { "CPS Counter", "C", "Render", "Renders the Clicks-Per-Second indicator hud.", &CPSCounter::g_showCpsCounter, nullptr },
        { "FPS Overlay", "F", "Render", "Shows frames-per-second count with optional graphs.", &FPSOverlay::g_showFpsOverlay, LLocal::toggleFPSOverlay },
        { "Ping Counter", "P", "Render", "Shows network latency/ping on HUD.", &PingCounter::g_showPingCounter, nullptr },
        { "Player Info", "N", "Render", "Displays your skin head and nickname on HUD.", &PlayerInfo::g_showPlayerInfo, nullptr },
        { "FullBright", "B", "Render", "Forces light levels to maximum brightness.", &FullBright::g_fullBrightEnabled, LLocal::toggleFullBright },
        { "MotionBlur", "M", "Render", "Adds a realistic screen motion blur effect.", &MotionBlur::g_motionBlurEnabled, nullptr },
        { "ClickGUI", "G", "Render", "Toggles and configures this ClickGUI overlay.", &ClickGUI::g_enabled, LLocal::toggleClickGUI },
        // Exploit
        { "UnlockFPS", "U", "Exploit", "Removes default FPS caps to run at maximum refresh.", &UnlockFPS::g_unlockFpsEnabled, nullptr },
        { "AutoClicker", "C", "Exploit", "Automatically clicks your mouse at a configurable speed.", &AutoClicker::g_enabled, nullptr },
        { "Anti-AFK", "K", "Exploit", "Performs background actions to prevent idle disconnects.", &AntiAFK::g_enabled, nullptr },
        { "Screenshot", "S", "Exploit", "Takes a screenshot of the game using the set hotkey.", &Screenshot::g_enabled, nullptr }
    };

    auto matchesQuery = [](const std::string& text, const std::string& q) -> bool {
        if (q.empty()) return true;
        std::string textLower = text;
        for (auto& c : textLower) c = tolower(c);
        std::string qLower = q;
        for (auto& c : qLower) c = tolower(c);
        return textLower.find(qLower) != std::string::npos;
    };

    static std::map<std::string, bool> expanded;
    static std::map<std::string, float> cardHover;
    static std::map<std::string, float> cardPop;
    static std::map<std::string, float> cardSw;
    static std::map<std::string, float> expandAnim;
    static std::map<std::string, float> measuredH;
    float dt = ImGui::GetIO().DeltaTime;
    ImVec4 accentV = GUI::g_colorAccent;
    ImU32 accentCol = ImGui::ColorConvertFloat4ToU32(accentV);
    float availWidth = ImGui::GetContentRegionAvail().x;

    const float spacing = 12.0f * sc;
    int cols = (availWidth >= 720.0f * sc) ? 3 : (availWidth >= 440.0f * sc ? 2 : 1);
    float cardW = (availWidth - spacing * (float)(cols - 1)) / (float)cols;
    float cardH = 96.0f * sc;

    int col = 0;
    int visibleIdx = 0;
    for (const auto& m : modules) {
        if (categoryFilter && strcmp(categoryFilter, "All") != 0 && m.category != categoryFilter) continue;
        if (query && strlen(query) > 0) {
            if (!matchesQuery(m.name, query) && !matchesQuery(m.category, query)) continue;
        }

        // Staggered card entrance (each card delays slightly)
        float delay = visibleIdx * 0.04f;
        visibleIdx++;
        float cardProgRaw = Animations::Clamp01((contentAnim - delay) / (1.0f - delay));
        float cardProg = Animations::EaseOutBack(cardProgRaw);
        float cProg = Animations::Clamp01(cardProg);
        float slideY = (1.0f - cardProgRaw) * 24.0f * sc;
        int cardAlpha = (int)(255.0f * cProg);

        if (col > 0) ImGui::SameLine(0.0f, spacing);
        ImGui::PushID(m.name);
        ImVec2 startPos = ImGui::GetCursorScreenPos();
        bool hasSettings = ModuleHasSettings(m.name);
        bool isExpanded = hasSettings && expanded[m.name];

        ImGui::InvisibleButton("##lunar_card", ImVec2(cardW, cardH));
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);

        if (clicked) { *m.enabled = !*m.enabled; cardPop[m.name] = 1.0f; if (m.callback) m.callback(); }
        if (rightClicked && hasSettings) {
            expanded[m.name] = !expanded[m.name];
        }

        // Per-card animated values
        float hProg = Animations::Approach(cardHover[m.name], hovered ? 1.0f : 0.0f, dt, 14.0f);
        cardHover[m.name] = hProg;
        float pop = Animations::Approach(cardPop[m.name], 0.0f, dt, 9.0f);
        cardPop[m.name] = pop;
        float swA = Animations::Approach(cardSw[m.name], *m.enabled ? 1.0f : 0.0f, dt, 13.0f);
        cardSw[m.name] = swA;

        // Smooth expand/collapse of the settings panel (frame-rate independent)
        float ea = Animations::Approach(expandAnim[m.name], isExpanded ? 1.0f : 0.0f, dt, 9.0f);
        expandAnim[m.name] = ea;
        float ease = Animations::EaseOutQuart(Animations::Clamp01(ea));

        ImDrawList* draw = ImGui::GetWindowDrawList();
        float r = 10.0f * sc;

        // Card background + hover grow
        float grow = hProg * 3.0f * sc;
        ImVec2 gMin = startPos - ImVec2(grow, grow);
        ImVec2 gMax = startPos + ImVec2(cardW, cardH) + ImVec2(grow, grow);
        float bgR, bgG, bgB;
        if (*m.enabled) { bgR = 38.0f + hProg * 8.0f; bgG = 40.0f + hProg * 9.0f; bgB = 47.0f + hProg * 10.0f; }
        else            { bgR = 33.0f + hProg * 8.0f; bgG = 35.0f + hProg * 8.0f; bgB = 41.0f + hProg * 9.0f; }
        draw->AddRectFilled(gMin, gMax, IM_COL32((int)bgR, (int)bgG, (int)bgB, cardAlpha), r);

        // Border glow (accent on hover)
        ImU32 borderCol;
        if (*m.enabled) borderCol = ImColor(accentV.x, accentV.y, accentV.z, (0.45f + 0.4f * hProg) * cProg);
        else borderCol = IM_COL32((int)(58 + hProg * 46), (int)(60 + hProg * 64), (int)(68 + hProg * 84), (int)(200 * cProg));
        draw->AddRect(gMin, gMax, borderCol, r, 0, (hovered || *m.enabled) ? 1.5f : 1.0f);

        // Toggle pulse ring
        if (pop > 0.01f && cProg > 0.1f) {
            float pulseW = 3.0f * sc * (1.0f - pop);
            draw->AddRect(gMin - ImVec2(pulseW, pulseW), gMax + ImVec2(pulseW, pulseW),
                ImColor(accentV.x, accentV.y, accentV.z, 0.65f * pop * cProg), r + pulseW, 0, 2.0f * sc);
        }

        // Icon tile (pops on hover / toggle)
        float iconScale = 1.0f + 0.10f * hProg + 0.16f * pop;
        ImVec2 iconCenter = startPos + ImVec2(36.0f * sc, 34.0f * sc + slideY);
        ImVec2 iconSize = ImVec2(40.0f * sc * iconScale, 40.0f * sc * iconScale);
        ImVec2 iconMin = iconCenter - ImVec2(iconSize.x * 0.5f, iconSize.y * 0.5f);
        ImVec2 iconMax = iconCenter + ImVec2(iconSize.x * 0.5f, iconSize.y * 0.5f);
        draw->AddRectFilled(iconMin, iconMax, ImColor(accentV.x, accentV.y, accentV.z, 0.13f * cProg), 9.0f * sc);
        draw->AddRect(iconMin, iconMax, ImColor(accentV.x, accentV.y, accentV.z, (0.28f + 0.25f * hProg) * cProg), 9.0f * sc, 0, 1.0f);
        ImGui::PushFont(GUI::g_fontH1 ? GUI::g_fontH1 : GUI::g_fontDefault);
        ImVec2 iconTextSize = ImGui::CalcTextSize(m.icon);
        draw->AddText(iconCenter - ImVec2(iconTextSize.x * 0.5f, iconTextSize.y * 0.5f), accentCol, m.icon);
        ImGui::PopFont();

        // Switch (animated knob + track color)
        float swW = 38.0f * sc, swH = 20.0f * sc;
        ImVec2 swPos = startPos + ImVec2(cardW - swW - 14.0f * sc, 24.0f * sc + slideY);
        int tr = (int)(accentV.x * 255.0f * swA + 62.0f * (1.0f - swA));
        int tg = (int)(accentV.y * 255.0f * swA + 64.0f * (1.0f - swA));
        int tb = (int)(accentV.z * 255.0f * swA + 72.0f * (1.0f - swA));
        draw->AddRectFilled(swPos, swPos + ImVec2(swW, swH), IM_COL32(tr, tg, tb, (int)(255.0f * cProg)), swH * 0.5f);
        draw->AddRect(swPos, swPos + ImVec2(swW, swH), IM_COL32(100, 102, 112, (int)(120.0f * cProg)), swH * 0.5f, 0, 1.0f);
        float knobR = swH * 0.5f - 3.0f * sc;
        float knobX = swPos.x + knobR + 3.0f * sc + (swW - 2.0f * (knobR + 3.0f * sc)) * swA;
        draw->AddCircleFilled(ImVec2(knobX, swPos.y + swH * 0.5f), knobR, IM_COL32(255, 255, 255, (int)(255.0f * cProg)));

        // Module name
        ImGui::PushFont(GUI::g_fontH3 ? GUI::g_fontH3 : GUI::g_fontDefault);
        draw->AddText(startPos + ImVec2(70.0f * sc, 15.0f * sc + slideY), IM_COL32(240, 240, 246, cardAlpha), m.name);
        ImGui::PopFont();

        // Description (wrapped to fit card)
        ImGui::PushFont(GUI::g_fontDefault);
        float descW = cardW - 70.0f * sc - 12.0f * sc;
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), startPos + ImVec2(70.0f * sc, 44.0f * sc + slideY), IM_COL32(142, 144, 152, cardAlpha), m.description, m.description + strlen(m.description), descW);
        ImGui::PopFont();

        col++;
        if (col == cols) { col = 0; ImGui::NewLine(); }

        // Expanded settings (right-click) — smooth height + fade panel
        if (ea > 0.01f) {
            float settingsTarget = 200.0f * sc;
            float natH = measuredH[m.name];
            if (natH <= 0.0f) natH = settingsTarget;
            if (natH < settingsTarget) settingsTarget = natH;
            float settingsH = settingsTarget * ease;

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ease);
            ImGui::SetCursorScreenPos(startPos + ImVec2(4.0f * sc, cardH + 6.0f * sc));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.11f, 0.115f, 0.13f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.5f, 0.5f, 0.55f, 0.55f * ease));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f * sc);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * sc, 6.0f * sc));
            std::string childId = std::string("##lunarcfg_") + m.name;
            if (ImGui::BeginChild(childId.c_str(), ImVec2(cardW - 8.0f * sc, settingsH), false, ImGuiWindowFlags_None)) {
                ImGui::PushItemWidth(cardW - 56.0f * sc);
                ClickGUI::RenderModuleSettings(m.name, cardW - 56.0f * sc);
                ImGui::PopItemWidth();
                measuredH[m.name] = ImGui::GetCursorPosY() + 8.0f;
            }
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(); // alpha

            // Reset the grid row so the next card starts on a clean line
            col = 0;
            ImGui::NewLine();
            ImGui::Spacing();
        }

        ImGui::PopID();
    }
}

void ClickGUI::RenderLunarMenu(float screenWidth, float screenHeight) {
    float e = Animations::EaseOutQuart(GUI::g_menuAnim);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, e);

    // Background blits
    if (g_bgStyle == 0) {
        ImU32 bgCol = IM_COL32(5, 5, 10, (int)(e * 180.0f));
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(screenWidth, screenHeight), bgCol);
    } else {
        ImU32 tint = IM_COL32(5, 5, 10, (int)(e * 60.0f));
        ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(screenWidth, screenHeight), tint);
    }

    if (g_showParticles) {
        GUI::RenderParticles(ImGui::GetBackgroundDrawList(), ImVec2(0, 0), ImVec2(screenWidth, screenHeight), e);
    }

    // Window entrance: back-overshoot scale + slide-up
    float scBack = Animations::EaseOutBack(e);
    float sc = 0.93f + (0.07f * scBack);
    ImVec2 baseSize = ImVec2(900, 580);
    ImVec2 winSize = ImVec2(baseSize.x * sc, baseSize.y * sc);
    ImVec2 winPos = ImVec2(screenWidth / 2 - winSize.x / 2,
                           screenHeight / 2 - winSize.y / 2 + (1.0f - e) * 26.0f * sc);

    ImGui::SetNextWindowSize(winSize, ImGuiCond_Always);
    ImGui::SetNextWindowPos(winPos, ImGuiCond_Always);

    // Shadow
    GUI::DrawShadow(ImGui::GetBackgroundDrawList(), winPos, winSize, 24.0f * sc, 30.0f * e, 0.45f * e);

    // Lunar window: nearly flat, small rounding, subtle border
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f * sc);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.070f, 0.075f, 0.088f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.16f, 0.17f, 0.20f, 0.7f));

    if (ImGui::Begin("LunarClickGUIWindow", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove)) {
        ImVec2 wPos = ImGui::GetWindowPos();
        ImVec2 wSize = ImGui::GetWindowSize();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec4 accentV = GUI::g_colorAccent;
        ImU32 accentCol = ImGui::ColorConvertFloat4ToU32(accentV);

        const float sidebarW = 205.0f * sc;
        const char* cats[] = { "Combat", "Movement", "Render", "Exploit" };

        // ── Animation state ──
        static int lunarTab = 0;
        if (lunarTab < 0 || lunarTab >= 4) lunarTab = 0;

        static float lunarOpenAnim = 1.0f;      // menu entrance stagger
        static float lunarContentAnim = 1.0f;   // tab switch entrance
        static int lastLunarTab = -1;
        static float lastMenuAnim = 0.0f;

        if (lastMenuAnim <= 0.01f && GUI::g_menuAnim > 0.01f) {
            lunarOpenAnim = 0.0f;
            lunarContentAnim = 0.0f;
            lastLunarTab = -1;
        }
        lastMenuAnim = GUI::g_menuAnim;

        if (lastLunarTab != lunarTab) {
            lunarContentAnim = 0.0f;
            lastLunarTab = lunarTab;
        }

        float dt = ImGui::GetIO().DeltaTime;
        lunarOpenAnim = Animations::Approach(lunarOpenAnim, 1.0f, dt, 5.0f);
        lunarContentAnim = Animations::Approach(lunarContentAnim, 1.0f, dt, 6.0f);
        float contentE = Animations::EaseOutQuart(Animations::Clamp01(lunarContentAnim));

        // Sidebar background + divider
        draw->AddRectFilled(wPos, wPos + ImVec2(sidebarW, wSize.y), ImColor(18, 19, 23, 255));
        draw->AddLine(wPos + ImVec2(sidebarW, 0.0f), wPos + ImVec2(sidebarW, wSize.y), ImColor(44, 46, 52, 200), 1.0f);

        // ── Brand header (slide from left + fade) ──
        float brandProg = Animations::EaseOutQuart(Animations::Clamp01(lunarOpenAnim / 0.5f));
        float brandX = -16.0f * sc * (1.0f - brandProg);
        int brandAlpha = (int)(255.0f * brandProg);
        ImGui::PushFont(GUI::g_fontH1 ? GUI::g_fontH1 : GUI::g_fontDefault);
        draw->AddText(wPos + ImVec2(20.0f * sc + brandX, 22.0f * sc), IM_COL32(245, 245, 250, brandAlpha), "AEGLESEEKER");
        ImGui::PopFont();
        ImGui::PushFont(GUI::g_fontDefault);
        draw->AddText(wPos + ImVec2(20.0f * sc + brandX, 52.0f * sc), ImColor(accentV.x, accentV.y, accentV.z, brandProg), "LUNAR EDITION");
        ImGui::PopFont();
        draw->AddLine(wPos + ImVec2(16.0f * sc, 78.0f * sc), wPos + ImVec2(sidebarW - 16.0f * sc, 78.0f * sc), IM_COL32(44, 46, 52, brandAlpha), 1.0f);

        // ── Sliding active indicator ──
        static float catActiveY = 94.0f * sc;
        catActiveY = Animations::Approach(catActiveY, 94.0f * sc + lunarTab * 46.0f * sc, dt, 16.0f);

        // ── Sidebar items (hover fade + staggered entrance) ──
        static float catHover[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        for (int i = 0; i < 4; i++) {
            float itemY = 94.0f * sc + i * 46.0f * sc;

            float itemProg = Animations::Clamp01((lunarOpenAnim - i * 0.07f) / (1.0f - i * 0.07f));
            itemProg = Animations::EaseOutBack(itemProg);
            int itemAlpha = (int)(255.0f * itemProg);
            float itemX = -14.0f * sc * (1.0f - itemProg);

            ImGui::SetCursorPos(ImVec2(12.0f * sc, itemY));
            ImGui::InvisibleButton(("##lunar_cat_" + std::to_string(i)).c_str(), ImVec2(sidebarW - 24.0f * sc, 38.0f * sc));
            bool hovered = ImGui::IsItemHovered();
            bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            if (clicked) lunarTab = i;
            catHover[i] = Animations::Approach(catHover[i], hovered ? 1.0f : 0.0f, dt, 12.0f);

            bool active = (lunarTab == i);

            if (catHover[i] > 0.01f && !active) {
                draw->AddRectFilled(wPos + ImVec2(14.0f * sc + itemX, itemY), wPos + ImVec2(sidebarW - 14.0f * sc, itemY + 38.0f * sc),
                    IM_COL32(255, 255, 255, (int)(14.0f * catHover[i] * itemProg)), 6.0f * sc);
            }

            float tr, tg, tb;
            if (active) { tr = 255.0f; tg = 255.0f; tb = 255.0f; }
            else        { tr = 175.0f + 80.0f * catHover[i]; tg = 177.0f + 78.0f * catHover[i]; tb = 186.0f + 69.0f * catHover[i]; }
            ImGui::PushFont(GUI::g_fontDefault);
            draw->AddText(wPos + ImVec2(28.0f * sc + itemX, itemY + 11.0f * sc), IM_COL32((int)tr, (int)tg, (int)tb, itemAlpha), cats[i]);
            ImGui::PopFont();
        }

        // Active pill (drawn on top, slides between categories)
        draw->AddRectFilled(wPos + ImVec2(14.0f * sc, catActiveY), wPos + ImVec2(sidebarW - 14.0f * sc, catActiveY + 38.0f * sc),
            ImColor(accentV.x, accentV.y, accentV.z, 0.16f), 6.0f * sc);
        draw->AddRectFilled(wPos + ImVec2(14.0f * sc, catActiveY), wPos + ImVec2(17.0f * sc, catActiveY + 38.0f * sc),
            ImColor(accentV.x, accentV.y, accentV.z, 1.0f), 6.0f * sc);

        // Content area
        ImGui::SetCursorPos(ImVec2(sidebarW + 18.0f * sc, 14.0f * sc));
        ImGui::BeginChild("LunarContentArea", ImVec2(wSize.x - sidebarW - 34.0f * sc, wSize.y - 28.0f * sc), false, ImGuiWindowFlags_None);
        {
            ImVec2 contentPos = ImGui::GetWindowPos();
            ImVec2 contentSize = ImGui::GetWindowSize();
            ImDrawList* cDraw = ImGui::GetWindowDrawList();
            float contentWidth = ImGui::GetContentRegionAvail().x;

            // Category header (slide-down + fade on tab switch)
            float hdrY = -10.0f * sc * (1.0f - contentE);
            int hdrAlpha = (int)(255.0f * contentE);
            ImGui::PushFont(GUI::g_fontH3 ? GUI::g_fontH3 : ImGui::GetFont());
            cDraw->AddText(contentPos + ImVec2(2.0f * sc, hdrY), IM_COL32(240, 240, 246, hdrAlpha), cats[lunarTab]);
            ImGui::PopFont();
            ImGui::PushFont(GUI::g_fontDefault);
            cDraw->AddText(contentPos + ImVec2(2.0f * sc, 26.0f * sc + hdrY), IM_COL32(140, 142, 150, hdrAlpha), "Right-click a module to open its settings");
            ImGui::PopFont();

            // Search bar
            static char lunarSearch[128] = "";
            ImGui::SetCursorPos(ImVec2(0.0f, 48.0f * sc + hdrY * 0.4f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.11f, 0.115f, 0.13f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.13f, 0.135f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.13f, 0.135f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.98f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.5f, 0.5f, 0.55f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f * sc);
            ImGui::SetNextItemWidth(contentWidth);
            ImGui::PushFont(GUI::g_fontDefault);
            ImGui::InputTextWithHint("##LunarSearch", "Search modules...", lunarSearch, IM_ARRAYSIZE(lunarSearch));
            ImGui::PopFont();

            // Search focus glow (animated accent ring)
            static float searchFocus = 0.0f;
            searchFocus = Animations::Approach(searchFocus, ImGui::IsItemActive() ? 1.0f : 0.0f, dt, 14.0f);
            if (searchFocus > 0.01f && contentE > 0.01f) {
                ImVec2 rMin = ImGui::GetItemRectMin();
                ImVec2 rMax = ImGui::GetItemRectMax();
                cDraw->AddRect(rMin - ImVec2(2.0f * sc, 2.0f * sc), rMax + ImVec2(2.0f * sc, 2.0f * sc),
                    ImColor(accentV.x, accentV.y, accentV.z, 0.30f * searchFocus * contentE), 8.0f * sc, 0, 1.5f * sc);
                cDraw->AddRectFilled(rMin, rMax, ImColor(accentV.x, accentV.y, accentV.z, 0.04f * searchFocus * contentE), 8.0f * sc);
            }

            ImGui::PopStyleVar();
            ImGui::PopStyleColor(5);

            // Module scroll list (staggered card entrance)
            ImGui::SetCursorPos(ImVec2(0.0f, 80.0f * sc + hdrY * 0.4f));
            ImGui::BeginChild("LunarModuleScroll", ImVec2(contentWidth, contentSize.y - 90.0f * sc), false, ImGuiWindowFlags_None);
            RenderLunarModulesList(cats[lunarTab], lunarSearch, sc, contentE);
            ImGui::EndChild();
        }
        ImGui::EndChild();
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleVar(); // alpha
}
