/*
Under an4rch Development Public Source License 1.0
*/

#include "MotionBlur.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../Animations/Animations.hpp"
#include "../../../GUI/GUI.hpp"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <d3dcompiler.h>

// Static member initialization
bool MotionBlur::g_motionBlurEnabled = false;
std::string MotionBlur::g_blurType = "Average Pixel Blur";
float MotionBlur::g_blurIntensity = 5.0f;
std::vector<ID3D11ShaderResourceView*> MotionBlur::g_previousFrames;
std::vector<float> MotionBlur::g_frameTimestamps;
float MotionBlur::g_blurTimeConstant = 0.0667f;
float MotionBlur::g_maxHistoryFrames = 8.0f;
bool MotionBlur::g_blurDynamicMode = false;
float MotionBlur::g_motionBlurAnim = 0.0f;
ULONGLONG MotionBlur::g_motionBlurEnableTime = 0;
ULONGLONG MotionBlur::g_motionBlurDisableTime = 0;

// Shader resources initialization
ID3D11PixelShader* MotionBlur::g_avg_pixelShader = nullptr;
ID3D11VertexShader* MotionBlur::g_avg_vertexShader = nullptr;
ID3D11InputLayout* MotionBlur::g_avg_inputLayout = nullptr;
ID3D11Buffer* MotionBlur::g_avg_constantBuffer = nullptr;
ID3D11Buffer* MotionBlur::g_avg_vertexBuffer = nullptr;
ID3D11DepthStencilState* MotionBlur::g_avg_depthStencilState = nullptr;
ID3D11BlendState* MotionBlur::g_avg_blendState = nullptr;
ID3D11RasterizerState* MotionBlur::g_avg_rasterizerState = nullptr;
ID3D11SamplerState* MotionBlur::g_avg_samplerState = nullptr;
bool MotionBlur::g_avg_hlperInitialized = false;

ID3D11PixelShader* MotionBlur::g_real_pixelShader = nullptr;
ID3D11VertexShader* MotionBlur::g_real_vertexShader = nullptr;
ID3D11InputLayout* MotionBlur::g_real_inputLayout = nullptr;
ID3D11Buffer* MotionBlur::g_real_constantBuffer = nullptr;
ID3D11Buffer* MotionBlur::g_real_vertexBuffer = nullptr;
ID3D11DepthStencilState* MotionBlur::g_real_depthStencilState = nullptr;
ID3D11BlendState* MotionBlur::g_real_blendState = nullptr;
ID3D11RasterizerState* MotionBlur::g_real_rasterizerState = nullptr;
ID3D11SamplerState* MotionBlur::g_real_samplerState = nullptr;
bool MotionBlur::g_real_helperInitialized = false;

// Backbuffer storage initialization
std::vector<ID3D11Texture2D*> MotionBlur::g_backbufferTextures;
std::vector<ID3D11ShaderResourceView*> MotionBlur::g_poolSRVs;
int MotionBlur::g_maxBackbufferFrames = 8;
int MotionBlur::g_currentBackbufferIndex = 0;
UINT MotionBlur::g_poolWidth = 0;
UINT MotionBlur::g_poolHeight = 0;
DXGI_FORMAT MotionBlur::g_poolFormat = DXGI_FORMAT_UNKNOWN;

// Forward declarations for helper functions

void MotionBlur::UpdateAnimation(ULONGLONG now) {
    // Motion Blur Animation - Fade in/out
    if (g_motionBlurEnabled && g_motionBlurEnableTime == 0) {
        g_motionBlurEnableTime = now;
        g_motionBlurDisableTime = 0;
    }
    if (!g_motionBlurEnabled && g_motionBlurDisableTime == 0 && g_motionBlurEnableTime > 0) {
        g_motionBlurDisableTime = now;
        g_motionBlurEnableTime = 0;
    }
    
    if (g_motionBlurEnableTime > 0) {
        float enableElapsed = (float)(now - g_motionBlurEnableTime) / 1000.0f;
        g_motionBlurAnim = fminf(1.0f, enableElapsed / 0.3f);
    }
    else if (g_motionBlurDisableTime > 0) {
        float disableElapsed = (float)(now - g_motionBlurDisableTime) / 1000.0f;
        float disableAnim = fminf(1.0f, disableElapsed / 0.2f);  // 200ms para desaparecer
        g_motionBlurAnim = 1.0f - disableAnim;
        if (disableAnim >= 1.0f) {
            g_motionBlurEnableTime = 0;
            g_motionBlurDisableTime = 0;
        }
    }
}

void MotionBlur::RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd) {
    // Motion Blur module in array list
    if (g_motionBlurEnabled || (g_motionBlurDisableTime > 0 && g_motionBlurAnim > 0.01f)) {
        float motionBlurAlpha = g_motionBlurAnim * 255.0f;
        float slideOffset = -60.0f + (Animations::SmoothInertia(g_motionBlurAnim) * 60.0f);
        
        if (motionBlurAlpha > 1.0f) {
            char mbBuf[64];
            sprintf_s(mbBuf, "Motion Blur");
            float wMB = ImGui::CalcTextSize(mbBuf).x;
            float xPosMB = arrayListStart.x + 290.0f - wMB - 10;  // 290.0f is typical array list width
            draw->AddText(ImVec2(xPosMB + slideOffset - 1, yPos + 1), IM_COL32(0, 0, 0, 220), mbBuf); // Sombra
            draw->AddText(ImVec2(xPosMB + slideOffset, yPos), IM_COL32(100, 255, 150, (int)motionBlurAlpha), mbBuf);
            yPos += 18.0f;
            arrayListEnd.y = yPos;
        }
    }
}

void MotionBlur::RenderMenu() {
    // Motion Blur warning and checkbox
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Unstable Feature - May Cause Performance Issues\nMotion Blur may cause performance issues on lower-end systems");
    GUI::RenderCustomSwitch("Motion Blur", &g_motionBlurEnabled);
    
    if (g_motionBlurEnabled) {
        ImGui::Separator();
        ImGui::Text("Motion Blur Settings");
        
        // Blur Type Dropdown
        static int blurTypeIndex = 0;
        static const char* blurTypes[] = { 
            "Average Pixel Blur",
            "Ghost Frames",
            "Time Aware Blur",
            "Real Motion Blur",
            "V4"
        };
        if (GUI::RenderCombo("Blur Type##MB", &blurTypeIndex, blurTypes, IM_ARRAYSIZE(blurTypes))) {
            g_blurType = blurTypes[blurTypeIndex];
        }
        
        // Intensity slider based on type
        if (g_blurType == "Time Aware Blur") {
            GUI::RenderSlider("Blur Time Constant##MB", &g_blurTimeConstant, 0.01f, 0.2f, "%.4f");
            GUI::RenderSlider("Max History Frames##MB", &g_maxHistoryFrames, 4.0f, 16.0f, "%.0f");
        } else {
            GUI::RenderSlider("Intensity##MB", &g_blurIntensity, 1.0f, 30.0f, "%.0f");
        }
        
        // Dynamic mode for Average Pixel Blur
        if (g_blurType == "Average Pixel Blur") {
            GUI::RenderCustomSwitch("Dynamic Mode##MB", &g_blurDynamicMode);
            if (g_blurDynamicMode) {
                ImGui::TextDisabled("Adjusts intensity based on FPS");
            }
        }
    }
}

// --- HLSL Shader Sources ---
const char* g_avgPixelShaderSrc = R"(
cbuffer FrameCountBuffer : register(b0) { int numFrames; float3 padding; };
Texture2D g_frames[50] : register(t0);
SamplerState g_sampler : register(s0);
struct VS_OUTPUT { float4 Pos : SV_POSITION; float2 Tex : TEXCOORD0; };
float4 mainPS(VS_OUTPUT input) : SV_Target {
    float4 color = float4(0, 0, 0, 0);
    int safeNumFrames = max(numFrames, 1);
    [unroll]
    for (int i = 0; i < safeNumFrames && i < 50; i++) {
        color += g_frames[i].Sample(g_sampler, input.Tex);
    }
    return color / safeNumFrames;
}
)";

const char* g_avgVertexShaderSrc = R"(
struct VS_INPUT { float3 Pos : POSITION; float2 Tex : TEXCOORD0; };
struct VS_OUTPUT { float4 Pos : SV_POSITION; float2 Tex : TEXCOORD0; };
VS_OUTPUT mainVS(VS_INPUT input) {
    VS_OUTPUT output;
    output.Pos = float4(input.Pos, 1.0);
    output.Tex = input.Tex;
    return output;
}
)";

const char* g_realVertexShaderSrc = R"(
cbuffer CameraDataBuffer : register(b0) {
    float4x4 preWorldViewProjection;
    float4x4 invWorldViewProjection;
    float intensity;
    int numSamples;
    float2 padding;
};
struct VS_INPUT { float3 Pos : POSITION; float2 Tex : TEXCOORD0; };
struct VS_OUTPUT { float4 Pos : SV_POSITION; float2 Tex : TEXCOORD0; };
VS_OUTPUT mainVS(VS_INPUT input) {
    VS_OUTPUT output;
    output.Pos = float4(input.Pos, 1.0);
    output.Tex = input.Tex;
    return output;
}
)";

const char* g_realPixelShaderSrc = R"(
cbuffer CameraDataBuffer : register(b0) {
    float4x4 preWorldViewProjection;
    float4x4 invWorldViewProjection;
    float intensity;
    int numSamples;
    float2 padding;
};
Texture2D g_velocityTex : register(t0);
Texture2D g_colorTex : register(t1);
SamplerState g_sampler : register(s0);
struct VS_OUTPUT { float4 Pos : SV_POSITION; float2 Tex : TEXCOORD0; };
float4 mainPS(VS_OUTPUT input) : SV_Target {
    float2 velocity = g_velocityTex.Sample(g_sampler, input.Tex).xy;
    float4 color = g_colorTex.Sample(g_sampler, input.Tex);
    float2 samplePos = input.Tex;
    int samples = max(numSamples, 1);
    [unroll]
    for(int i = 1; i < samples && i < 16; i++) {
        float t = (float)i / (float)samples;
        samplePos -= velocity * intensity * 0.1;
        color += g_colorTex.Sample(g_sampler, samplePos);
    }
    return color / samples;
}
)";

// --- Shader Compilation ---
bool CompileMotionBlurShader(const char* srcData, const char* entryPoint, const char* shaderModel, ID3DBlob** blobOut) {
    UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
    compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ID3DBlob* errorBlob = nullptr;
    HRESULT hr = D3DCompile(srcData, strlen(srcData), nullptr, nullptr, nullptr, entryPoint, shaderModel, compileFlags, 0, blobOut, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        return false;
    }
    if (errorBlob) errorBlob->Release();
    return true;
}

// --- Backbuffer Management ---

// Maximum number of history frames the pool can hold (also the SRV cap for ps_5_0 sampling)
static const int kMaxPoolFrames = 16;

// Convert a possibly typeless/sRGB backbuffer format into a typed SRV-able format.
// The pool texture keeps the backbuffer format (required by CopyResource) but the SRV
// must use a fully-typed format, otherwise CreateShaderResourceView fails.
static DXGI_FORMAT MotionBlurMakeSrvFormat(DXGI_FORMAT f) {
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

void MotionBlur::InitializeBackbufferStorage(int maxFrames) {
    (void)maxFrames; // capacity is fixed; the caller clamps frame usage separately
    if ((int)g_backbufferTextures.size() == kMaxPoolFrames) return;

    for (auto t : g_backbufferTextures) if (t) t->Release();
    for (auto s : g_poolSRVs) if (s) s->Release();
    g_backbufferTextures.assign(kMaxPoolFrames, nullptr);
    g_poolSRVs.assign(kMaxPoolFrames, nullptr);
    g_maxBackbufferFrames = kMaxPoolFrames;
    g_currentBackbufferIndex = 0;
    g_poolWidth = 0;
    g_poolHeight = 0;
    g_poolFormat = DXGI_FORMAT_UNKNOWN;

    // g_previousFrames holds non-owning pool SRV pointers; just drop the ordering
    g_previousFrames.clear();
    g_frameTimestamps.clear();
}

ID3D11ShaderResourceView* MotionBlur::CopyBackbufferToSRV(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, IDXGISwapChain* pSwapChain) {
    if (!pSwapChain || !pDevice || !pContext) return nullptr;

    if (g_backbufferTextures.empty()) {
        InitializeBackbufferStorage(kMaxPoolFrames);
        if (g_backbufferTextures.empty()) return nullptr;
    }

    ID3D11Texture2D* pBackBuffer = nullptr;
    if (FAILED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer))) {
        return nullptr;
    }

    D3D11_TEXTURE2D_DESC desc;
    pBackBuffer->GetDesc(&desc);

    // Recreate the whole pool if the resolution or format changed
    if (g_poolWidth != desc.Width || g_poolHeight != desc.Height || g_poolFormat != desc.Format) {
        for (auto t : g_backbufferTextures) if (t) { t->Release(); t = nullptr; }
        for (auto s : g_poolSRVs) if (s) { s->Release(); s = nullptr; }
        g_poolWidth  = desc.Width;
        g_poolHeight = desc.Height;
        g_poolFormat = desc.Format;
        g_currentBackbufferIndex = 0;
        g_previousFrames.clear();
        g_frameTimestamps.clear();
    }

    int poolSize = (int)g_backbufferTextures.size();
    int idx = g_currentBackbufferIndex % poolSize;

    if (!g_backbufferTextures[idx]) {
        D3D11_TEXTURE2D_DESC texDesc = desc;
        texDesc.Usage          = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
        texDesc.CPUAccessFlags = 0;
        texDesc.MiscFlags      = 0;
        if (FAILED(pDevice->CreateTexture2D(&texDesc, nullptr, &g_backbufferTextures[idx]))) {
            pBackBuffer->Release();
            return nullptr;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = MotionBlurMakeSrvFormat(desc.Format);
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels       = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;
        if (FAILED(pDevice->CreateShaderResourceView(g_backbufferTextures[idx], &srvDesc, &g_poolSRVs[idx]))) {
            g_backbufferTextures[idx]->Release(); g_backbufferTextures[idx] = nullptr;
            pBackBuffer->Release();
            return nullptr;
        }
    }

    pContext->CopyResource(g_backbufferTextures[idx], pBackBuffer);
    pBackBuffer->Release();

    ID3D11ShaderResourceView* srv = g_poolSRVs[idx];
    // Advance the ring only on a successful capture so the slot ordering stays
    // aligned with g_previousFrames.
    g_currentBackbufferIndex++;
    return srv;
}

void MotionBlur::CleanupBackbufferStorage() {
    for (auto tex : g_backbufferTextures) {
        if (tex) tex->Release();
    }
    g_backbufferTextures.clear();

    for (auto srv : g_poolSRVs) {
        if (srv) srv->Release();
    }
    g_poolSRVs.clear();

    // g_previousFrames are non-owning pool SRVs, just drop the ordering
    g_previousFrames.clear();
    g_frameTimestamps.clear();
    g_currentBackbufferIndex = 0;
    g_poolWidth = 0;
    g_poolHeight = 0;
    g_poolFormat = DXGI_FORMAT_UNKNOWN;
}

void MotionBlur::OnPresent(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, IDXGISwapChain* pSwapChain) {
    if (!g_motionBlurEnabled) return;

    int maxFrames = 1;
    if (g_blurType == "Time Aware Blur") {
        maxFrames = (int)round(g_maxHistoryFrames);
    } else if (g_blurType == "Real Motion Blur") {
        maxFrames = 8;
    } else {
        // At least 6 frames so the trail is clearly visible even at low intensity
        maxFrames = max(6, (int)round(g_blurIntensity));
    }

    if (maxFrames <= 0) maxFrames = 6;
    if (maxFrames > 16) maxFrames = 16;

    InitializeBackbufferStorage(maxFrames);
    ID3D11ShaderResourceView* srv = CopyBackbufferToSRV(pDevice, pContext, pSwapChain);
    if (srv) {
        if ((int)g_previousFrames.size() >= maxFrames) {
            // The pool owns the SRVs; just drop the oldest ordering entry
            g_previousFrames.erase(g_previousFrames.begin());
            g_frameTimestamps.erase(g_frameTimestamps.begin());
        }

        g_previousFrames.push_back(srv);
        g_frameTimestamps.push_back((float)GetTickCount64() / 1000.0f);
    }
}

void MotionBlur::RenderTrail(bool menuOpen) {
    // Release the frame pool when the module is turned off
    static bool wasMotionBlurEnabled = false;
    if (!g_motionBlurEnabled && wasMotionBlurEnabled) {
        CleanupBackbufferStorage();
    }
    wasMotionBlurEnabled = g_motionBlurEnabled;

    if (!g_motionBlurEnabled || menuOpen || g_previousFrames.size() == 0 || g_motionBlurAnim <= 0.01f) {
        return;
    }

    float currentTime = (float)GetTickCount64() / 1000.0f;
    ImVec2 screenSize = ImGui::GetIO().DisplaySize;
    ImDrawList* blurDraw = ImGui::GetBackgroundDrawList();
    float anim = g_motionBlurAnim;

    const auto& frames = g_previousFrames;
    const auto& times  = g_frameTimestamps;
    size_t n = frames.size();

    // Per-type trail parameters. The newest history frame is the strongest and
    // older frames fade out smoothly, producing a continuous motion trail
    // instead of discrete backward ghosts.
    float baseAlpha = 0.50f;
    float decay     = 0.84f;
    bool  timeAware = false;

    if (g_blurType == "Average Pixel Blur") {
        baseAlpha = 0.50f; decay = 0.84f;
    } else if (g_blurType == "Ghost Frames") {
        baseAlpha = 0.40f; decay = 0.60f;
    } else if (g_blurType == "Time Aware Blur") {
        timeAware = true; baseAlpha = 0.50f;
    } else if (g_blurType == "Real Motion Blur") {
        baseAlpha = 0.55f; decay = 0.78f;
    } else { // V4
        baseAlpha = 0.50f; decay = 0.80f;
    }

    // Dynamic mode: keep the trail inside a fixed ~100ms time window so the
    // perceived blur length stays constant regardless of frame rate.
    size_t first = 0;
    if (g_blurDynamicMode && times.size() == n) {
        size_t count = n;
        for (size_t i = n; i-- > 0;) {
            if (currentTime - times[i] > 0.10f) { count = n - 1 - i; break; }
        }
        if (count < 2) count = 2;
        first = n - count;
    }

    // Intensity scales the trail strength
    float strength = fminf(1.15f, 0.65f + g_blurIntensity * 0.02f);

    // Build normalized weights over the active range (sum == 1)
    std::vector<float> weights(n, 0.0f);
    float totalWeight = 0.0f;
    for (size_t i = first; i < n; i++) {
        float w;
        if (timeAware) {
            float age = currentTime - times[i];
            w = expf(-age / g_blurTimeConstant);
        } else {
            w = powf(decay, (float)(n - 1 - i));
        }
        weights[i] = w;
        totalWeight += w;
    }
    if (totalWeight <= 0.0f) totalWeight = 1.0f;

    // Draw oldest -> newest so the recent trail sits on top
    for (size_t i = first; i < n; i++) {
        ID3D11ShaderResourceView* frame = frames[i];
        if (!frame || weights[i] <= 0.0f) continue;
        float alpha = baseAlpha * (weights[i] / totalWeight) * strength * anim;
        if (alpha <= 0.003f) continue;
        if (alpha > 1.0f) alpha = 1.0f;
        ImU32 col = IM_COL32(255, 255, 255, (int)(alpha * 255.0f));
        blurDraw->AddImage((ImTextureID)frame, ImVec2(0, 0), screenSize, ImVec2(0, 0), ImVec2(1, 1), col);
    }
}
