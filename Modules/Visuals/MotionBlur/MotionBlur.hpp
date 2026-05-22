/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <d3d11.h>
#include <string>
#include <vector>
#include <windows.h>

// Forward declarations
class ImDrawList;
struct ImVec2;

/// @brief Motion Blur module - Multiple blur algorithms (Average Pixel, Ghost Frames, Time Aware, Real Motion Blur)
class MotionBlur {
public:
    // Static member variables for state
    static bool g_motionBlurEnabled;
    static std::string g_blurType;
    static float g_blurIntensity;
    static std::vector<ID3D11ShaderResourceView*> g_previousFrames;
    static std::vector<float> g_frameTimestamps;
    static float g_blurTimeConstant;
    static float g_maxHistoryFrames;
    static bool g_blurDynamicMode;
    static float g_motionBlurAnim;
    static ULONGLONG g_motionBlurEnableTime;
    static ULONGLONG g_motionBlurDisableTime;

    // Shader resources
    static ID3D11PixelShader* g_avg_pixelShader;
    static ID3D11VertexShader* g_avg_vertexShader;
    static ID3D11InputLayout* g_avg_inputLayout;
    static ID3D11Buffer* g_avg_constantBuffer;
    static ID3D11Buffer* g_avg_vertexBuffer;
    static ID3D11DepthStencilState* g_avg_depthStencilState;
    static ID3D11BlendState* g_avg_blendState;
    static ID3D11RasterizerState* g_avg_rasterizerState;
    static ID3D11SamplerState* g_avg_samplerState;
    static bool g_avg_hlperInitialized;

    static ID3D11PixelShader* g_real_pixelShader;
    static ID3D11VertexShader* g_real_vertexShader;
    static ID3D11InputLayout* g_real_inputLayout;
    static ID3D11Buffer* g_real_constantBuffer;
    static ID3D11Buffer* g_real_vertexBuffer;
    static ID3D11DepthStencilState* g_real_depthStencilState;
    static ID3D11BlendState* g_real_blendState;
    static ID3D11RasterizerState* g_real_rasterizerState;
    static ID3D11SamplerState* g_real_samplerState;
    static bool g_real_helperInitialized;

    // Backbuffer storage
    static std::vector<ID3D11Texture2D*> g_backbufferTextures;
    static int g_maxBackbufferFrames;
    static int g_currentBackbufferIndex;

    /// @brief Update animation state (call from main render loop)
    static void UpdateAnimation(ULONGLONG now);

    /// @brief Render motion blur in array list (for HUD display)
    static void RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd);

    /// @brief Render menu controls for motion blur settings
    static void RenderMenu();

    /// @brief Initialize backbuffer storage for motion blur
    static void InitializeBackbufferStorage(int maxFrames);

    /// @brief Copy backbuffer to SRV (Shader Resource View)
    static ID3D11ShaderResourceView* CopyBackbufferToSRV(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, IDXGISwapChain* pSwapChain);

    /// @brief Clean up backbuffer storage
    static void CleanupBackbufferStorage();
};
