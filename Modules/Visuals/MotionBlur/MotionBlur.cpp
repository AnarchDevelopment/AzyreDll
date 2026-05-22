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
#include <d3dcompiler.h>

// Static member initialization
bool MotionBlur::g_motionBlurEnabled = false;
std::string MotionBlur::g_blurType = "Average Pixel Blur";
float MotionBlur::g_blurIntensity = 3.0f;
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
int MotionBlur::g_maxBackbufferFrames = 8;
int MotionBlur::g_currentBackbufferIndex = 0;

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
        if (ImGui::Combo("Blur Type##MB", &blurTypeIndex, blurTypes, IM_ARRAYSIZE(blurTypes))) {
            g_blurType = blurTypes[blurTypeIndex];
        }
        
        // Intensity slider based on type
        if (g_blurType == "Time Aware Blur") {
            ImGui::SliderFloat("Blur Time Constant##MB", &g_blurTimeConstant, 0.01f, 0.2f, "%.4f");
            ImGui::SliderFloat("Max History Frames##MB", &g_maxHistoryFrames, 4.0f, 16.0f, "%.0f");
        } else {
            ImGui::SliderFloat("Intensity##MB", &g_blurIntensity, 1.0f, 30.0f, "%.0f");
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
void MotionBlur::InitializeBackbufferStorage(int maxFrames) {
    if (g_backbufferTextures.size() >= (size_t)maxFrames) return;
    while (g_backbufferTextures.size() < (size_t)maxFrames) {
        g_backbufferTextures.push_back(nullptr);
    }
    g_maxBackbufferFrames = maxFrames;
}

ID3D11ShaderResourceView* MotionBlur::CopyBackbufferToSRV(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, IDXGISwapChain* pSwapChain) {
    if (!pSwapChain || !pDevice || !pContext) return nullptr;
    
    ID3D11Texture2D* pBackBuffer = nullptr;
    if (FAILED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer))) {
        return nullptr;
    }
    
    D3D11_TEXTURE2D_DESC desc;
    pBackBuffer->GetDesc(&desc);
    
    D3D11_TEXTURE2D_DESC texDesc = desc;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    texDesc.CPUAccessFlags = 0;
    
    ID3D11Texture2D* pTexture = nullptr;
    if (FAILED(pDevice->CreateTexture2D(&texDesc, nullptr, &pTexture))) {
        pBackBuffer->Release();
        return nullptr;
    }
    
    pContext->CopyResource(pTexture, pBackBuffer);
    
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;
    
    ID3D11ShaderResourceView* pSRV = nullptr;
    if (FAILED(pDevice->CreateShaderResourceView(pTexture, &srvDesc, &pSRV))) {
        pTexture->Release();
        pBackBuffer->Release();
        return nullptr;
    }
    
    pTexture->Release();
    pBackBuffer->Release();
    
    return pSRV;
}

void MotionBlur::CleanupBackbufferStorage() {
    for (auto tex : g_backbufferTextures) {
        if (tex) tex->Release();
    }
    g_backbufferTextures.clear();
    
    for (auto srv : g_previousFrames) {
        if (srv) srv->Release();
    }
    g_previousFrames.clear();
    g_frameTimestamps.clear();
    g_currentBackbufferIndex = 0;
}
