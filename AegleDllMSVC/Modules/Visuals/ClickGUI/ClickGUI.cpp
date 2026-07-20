/*
Under an4rch Development Public Source License 1.0
*/
#define IMGUI_DEFINE_MATH_OPERATORS
#include "ClickGUI.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../GUI/GUI.hpp"
#include "../../../Animations/Animations.hpp"
#include "../../ModuleHeader.hpp"
#include "../../../ArrayList/ArrayList.hpp"
#include "../../../Networking/IRChat.hpp"
#include "../../../Config/ConfigManager.hpp"
#include "../../../Assets/resource.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <map>
#include <string>
#include <cstring>

extern HMODULE g_hModule;

// ──────────────────────────────────────────────
// Static member initialization
// ──────────────────────────────────────────────
bool  ClickGUI::g_enabled        = false;
int   ClickGUI::g_guiStyle       = 0;
bool  ClickGUI::g_showParticles  = true;
float ClickGUI::g_bgOpacity      = 0.7f;
int   ClickGUI::g_bgStyle        = 0;   // 0 = Normal, 1 = Blurred
float ClickGUI::g_blurRadius     = 4.0f;
float ClickGUI::g_blurOpacity    = 0.85f;
bool  ClickGUI::g_blurShadersReady = false;
std::map<std::string, bool> ClickGUI::g_expandedModules;

// DX11 blur pipeline
ID3D11PixelShader*       ClickGUI::g_blurPS      = nullptr;
ID3D11VertexShader*      ClickGUI::g_blurVS      = nullptr;
ID3D11InputLayout*       ClickGUI::g_blurIL      = nullptr;
ID3D11Buffer*            ClickGUI::g_blurVB      = nullptr;
ID3D11Buffer*            ClickGUI::g_blurCB      = nullptr;
ID3D11SamplerState*      ClickGUI::g_blurSampler = nullptr;
ID3D11BlendState*        ClickGUI::g_blurBlendState = nullptr;
ID3D11DepthStencilState* ClickGUI::g_blurDSS     = nullptr;
ID3D11RasterizerState*   ClickGUI::g_blurRS      = nullptr;
ID3D11ShaderResourceView* ClickGUI::g_sceneSRV   = nullptr;
ID3D11Texture2D*         ClickGUI::g_sceneTexture = nullptr;
UINT ClickGUI::g_capturedWidth  = 0;
UINT ClickGUI::g_capturedHeight = 0;

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
    float2 texelSize;
    float  blurRadius;
    float  opacity;
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

float4 mainPS(VS_OUTPUT input) : SV_Target {
    float4 colH = float4(0,0,0,0);
    float4 colV = float4(0,0,0,0);
    [unroll]
    for (int i = 0; i < SAMPLES; i++) {
        colH += g_scene.Sample(g_sampler, input.Tex + float2(offsets[i]*blurRadius*texelSize.x, 0.0)) * weights[i];
        colV += g_scene.Sample(g_sampler, input.Tex + float2(0.0, offsets[i]*blurRadius*texelSize.y)) * weights[i];
    }
    float4 color = (colH + colV) * 0.5;
    color.rgb *= (1.0 - opacity * 0.35);
    color.a = 1.0;
    return color;
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

// ──────────────────────────────────────────────
// Initialize / Shutdown blur pipeline
// ──────────────────────────────────────────────
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

    // --- Pixel Shader ---
    ID3DBlob* psBlob = nullptr;
    if (!CompileBlurShader(s_blurPsSrc, "mainPS", "ps_5_0", &psBlob)) return;
    pDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &g_blurPS);
    psBlob->Release();

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

    // --- Constant Buffer (BlurParams: texelSize, blurRadius, opacity) ---
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth      = sizeof(float) * 4; // float2 texelSize + float blurRadius + float opacity
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

    g_blurShadersReady = true;
}

void ClickGUI::ShutdownBlurShaders() {
    auto safe = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    safe(g_blurPS); safe(g_blurVS); safe(g_blurIL); safe(g_blurVB);
    safe(g_blurCB); safe(g_blurSampler); safe(g_blurBlendState);
    safe(g_blurDSS); safe(g_blurRS); safe(g_sceneSRV); safe(g_sceneTexture);
    g_blurShadersReady = false;
}

// ──────────────────────────────────────────────
// Render blur over the full screen
// ──────────────────────────────────────────────
void ClickGUI::RenderBlurBackground(ID3D11Device* pDevice, ID3D11DeviceContext* pContext,
                                     IDXGISwapChain* pSwapChain,
                                     float screenWidth, float screenHeight, float menuAnim) {
    if (!g_blurShadersReady || menuAnim <= 0.001f) return;
    if (!pDevice || !pContext || !pSwapChain) return;

    float e = Animations::EaseOutQuart(menuAnim);

    // ---- Capture current backbuffer (scene before ImGui) ----
    ID3D11Texture2D* pBack = nullptr;
    if (FAILED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBack))) return;

    D3D11_TEXTURE2D_DESC desc;
    pBack->GetDesc(&desc);

    // Re-create capture texture only if resolution changed
    if (!g_sceneTexture || g_capturedWidth != desc.Width || g_capturedHeight != desc.Height) {
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
        D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
        srvd.Format                    = desc.Format;
        srvd.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvd.Texture2D.MipLevels       = 1;
        srvd.Texture2D.MostDetailedMip = 0;
        if (FAILED(pDevice->CreateShaderResourceView(g_sceneTexture, &srvd, &g_sceneSRV))) {
            pBack->Release(); return;
        }
        g_capturedWidth  = desc.Width;
        g_capturedHeight = desc.Height;
    }

    pContext->CopyResource(g_sceneTexture, pBack);
    pBack->Release();

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

    // ---- Set blur pipeline ----
    D3D11_VIEWPORT vp = { 0.0f, 0.0f, screenWidth, screenHeight, 0.0f, 1.0f };
    pContext->RSSetViewports(1, &vp);

    // Update constant buffer
    D3D11_MAPPED_SUBRESOURCE ms;
    if (SUCCEEDED(pContext->Map(g_blurCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        float* data = (float*)ms.pData;
        data[0] = 1.0f / screenWidth;   // texelSize.x
        data[1] = 1.0f / screenHeight;  // texelSize.y
        data[2] = g_blurRadius * e;     // blurRadius (scales with anim)
        data[3] = g_blurOpacity;        // overlay opacity
        pContext->Unmap(g_blurCB, 0);
    }

    UINT stride = sizeof(float) * 5, offset = 0;
    pContext->IASetVertexBuffers(0, 1, &g_blurVB, &stride, &offset);
    pContext->IASetInputLayout(g_blurIL);
    pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    pContext->VSSetShader(g_blurVS, nullptr, 0);
    pContext->PSSetShader(g_blurPS, nullptr, 0);
    pContext->PSSetConstantBuffers(0, 1, &g_blurCB);
    pContext->PSSetSamplers(0, 1, &g_blurSampler);
    pContext->PSSetShaderResources(0, 1, &g_sceneSRV);
    pContext->OMSetBlendState(g_blurBlendState, nullptr, 0xFFFFFFFF);
    pContext->OMSetDepthStencilState(g_blurDSS, 0);
    pContext->RSSetState(g_blurRS);
    pContext->OMSetRenderTargets(1, &oldRTV, nullptr);

    pContext->Draw(4, 0);

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

    // Release saved refs
    auto sr = [](auto* p) { if (p) p->Release(); };
    sr(oldRTV); sr(oldDSV); sr(oldVB); sr(oldIL);
    sr(oldVS); sr(oldPS); sr(oldCB); sr(oldSS); sr(oldSRV);
    sr(oldBS); sr(oldDSS); sr(oldRS);
}

// ──────────────────────────────────────────────
// Module lifecycle
// ──────────────────────────────────────────────
void ClickGUI::Initialize() {
    g_enabled       = false;
    g_guiStyle      = 0;
    g_showParticles = true;
    g_bgOpacity     = 0.7f;
    g_bgStyle       = 0;
    g_blurRadius    = 4.0f;
    g_blurOpacity   = 0.85f;
    g_expandedModules.clear();
}

void ClickGUI::RenderMenu() {
    GUI::RenderCustomSwitch("ClickGUI Module", &g_enabled);

    if (GUI::BeginModuleSettings("ClickGUI", &g_enabled)) {
        const char* styles[] = { "Regular", "Separated", "Rise" };
        ImGui::Combo("GUI Style", &g_guiStyle, styles, IM_ARRAYSIZE(styles));

        const char* bgStyles[] = { "Normal", "Blurred Background" };
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
        static void toggleClickGUI() { GUI::g_showMenu = ClickGUI::g_enabled; }
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
void ClickGUI::RenderModuleButton(const char* label, bool* enabledPtr, void (*toggleCallback)()) {
    ImGui::PushID(label);

    float btnHeight = 32.0f;
    float w = ImGui::GetContentRegionAvail().x - 16.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    bool active = *enabledPtr;

    // Enabled animation
    std::string key = "btn_anim_" + std::string(label);
    if (GUI::g_elementAnims.find(key) == GUI::g_elementAnims.end())
        GUI::g_elementAnims[key] = active ? 1.0f : 0.0f;
    float target = active ? 1.0f : 0.0f;
    GUI::g_elementAnims[key] += (target - GUI::g_elementAnims[key]) * 0.2f;
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

    // RIGHT = expand/collapse settings
    if (rightClicked) {
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

    // Animated expand/collapse settings
    std::string expandKey = "expand_" + std::string(label);
    bool expanded = g_expandedModules.count(label) ? g_expandedModules[label] : false;
    if (GUI::g_elementAnims.find(expandKey) == GUI::g_elementAnims.end())
        GUI::g_elementAnims[expandKey] = 0.0f;
    float expandTarget = expanded ? 1.0f : 0.0f;
    GUI::g_elementAnims[expandKey] += (expandTarget - GUI::g_elementAnims[expandKey]) * 0.15f;
    float ea = GUI::g_elementAnims[expandKey];

    if (ea > 0.01f) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ea);
        float slideOff = (1.0f - ea) * 8.0f;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - slideOff);

        ImGui::Spacing();
        ImGui::Indent(12.0f);
        ImGui::PushItemWidth(w - 24.0f);

        bool before = *enabledPtr;
        GUI::RenderCustomSwitch("Enabled", enabledPtr);
        if (*enabledPtr != before && toggleCallback) toggleCallback();

        ImGui::Spacing();
        RenderModuleSettings(label, w - 24.0f);

        ImGui::PopItemWidth();
        ImGui::Unindent(12.0f);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PopStyleVar();
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
        ImGui::SliderFloat("Limit##F", &UnlockFPS::g_fpsLimit, 30.0f, 1000.0f, "%.0f");
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
    } else if (strcmp(name, "ClickGUI") == 0) {
        static const char* st[] = {"Regular","Separated"};
        ImGui::Combo("Style##CG", &ClickGUI::g_guiStyle, st, 2);
        static const char* bg[] = {"Normal","Blurred Background"};
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
        static void toggleClickGUI() { GUI::g_showMenu = ClickGUI::g_enabled; }
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
    
    float availWidth = ImGui::GetContentRegionAvail().x - 15.0f;
    
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
        
        bool expanded = expandedCards[module.name];
        
        // Settings child height: fixed per-module or default 160px
        // This controls how tall the settings panel is when expanded.
        float settingsChildH = 0.0f;
        if (expanded) {
            if (strcmp(module.name.c_str(), "Keystrokes") == 0)   settingsChildH = 400.0f * sc;
            else if (strcmp(module.name.c_str(), "ArrayList") == 0)    settingsChildH = 200.0f * sc;
            else if (strcmp(module.name.c_str(), "FPS Overlay") == 0)  settingsChildH = 150.0f * sc;
            else if (strcmp(module.name.c_str(), "ClickGUI") == 0)     settingsChildH = 150.0f * sc;
            else if (strcmp(module.name.c_str(), "CPS Counter") == 0)  settingsChildH = 110.0f * sc;
            else if (strcmp(module.name.c_str(), "Render Info") == 0)  settingsChildH = 110.0f * sc;
            else if (strcmp(module.name.c_str(), "Ping Counter") == 0) settingsChildH = 110.0f * sc;
            else if (strcmp(module.name.c_str(), "AutoClicker") == 0)  settingsChildH = 135.0f * sc;
            else if (strcmp(module.name.c_str(), "Anti-AFK") == 0)     settingsChildH = 135.0f * sc;
            else if (strcmp(module.name.c_str(), "Screenshot") == 0)   settingsChildH = 110.0f * sc;
            else settingsChildH = 80.0f * sc; // small slider/switch modules
        }
        
        float headerHeight = 70.0f * sc;
        float cardHeight   = headerHeight + (expanded ? (settingsChildH + 12.0f * sc) : 0.0f);
        
        // === Header interaction area only — does NOT cover expanded settings ===
        ImGui::InvisibleButton("##card_header", ImVec2(availWidth, headerHeight));
        bool hovered      = ImGui::IsItemHovered();
        bool leftClicked  = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        
        if (leftClicked) {
            *module.enabled = !*module.enabled;
            if (module.callback) module.callback();
        }
        if (rightClicked) {
            expandedCards[module.name] = !expandedCards[module.name];
            expanded = expandedCards[module.name];
            // Recalculate settingsChildH and cardHeight after toggling
            if (expanded) {
                if (strcmp(module.name.c_str(), "Keystrokes") == 0)   settingsChildH = 400.0f * sc;
                else if (strcmp(module.name.c_str(), "ArrayList") == 0)    settingsChildH = 200.0f * sc;
                else if (strcmp(module.name.c_str(), "FPS Overlay") == 0)  settingsChildH = 150.0f * sc;
                else if (strcmp(module.name.c_str(), "ClickGUI") == 0)     settingsChildH = 150.0f * sc;
                else if (strcmp(module.name.c_str(), "CPS Counter") == 0)  settingsChildH = 110.0f * sc;
                else if (strcmp(module.name.c_str(), "Render Info") == 0)  settingsChildH = 110.0f * sc;
                else if (strcmp(module.name.c_str(), "Ping Counter") == 0) settingsChildH = 110.0f * sc;
                else if (strcmp(module.name.c_str(), "AutoClicker") == 0)  settingsChildH = 135.0f * sc;
                else if (strcmp(module.name.c_str(), "Anti-AFK") == 0)     settingsChildH = 135.0f * sc;
                else if (strcmp(module.name.c_str(), "Screenshot") == 0)   settingsChildH = 110.0f * sc;
                else settingsChildH = 80.0f * sc;
            } else {
                settingsChildH = 0.0f;
            }
            cardHeight = headerHeight + (expanded ? (settingsChildH + 12.0f * sc) : 0.0f);
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
        if (expanded && settingsChildH > 0.0f) {
            // Position settings child window right below the header
            ImGui::SetCursorScreenPos(startPos + ImVec2(8.0f * sc, headerHeight + 6.0f * sc));
            
            std::string childId = std::string("##cfg_") + module.name;
            ImGui::PushStyleColor(ImGuiCol_ChildBg,   ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0,0,0,0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f * sc, 2.0f * sc));
            
            // BeginChild with explicit size — scrollbar appears automatically when content overflows
            bool childOk = ImGui::BeginChild(childId.c_str(),
                ImVec2(availWidth - 16.0f * sc, settingsChildH),
                false,
                ImGuiWindowFlags_None);
            if (childOk) {
                ImGui::PushItemWidth(availWidth - 56.0f * sc);
                ClickGUI::RenderModuleSettings(module.name.c_str(), availWidth - 56.0f * sc);
                ImGui::PopItemWidth();
            }
            ImGui::EndChild();  // After EndChild, parent cursor is correctly positioned below the child
            
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
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
                    
                    const char* bgStyles[] = { "Normal Dark", "Blurred Background" };
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
