/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <windows.h>

struct ImDrawList;
struct ImVec2;
struct ID3D11Device;
struct IDXGISwapChain;
struct IDXGISwapChain3;

/// @brief UnlockFPS module - Real UWP FPS unlock (vsync off + waitable swapchain latency 0 + frame queue 1) with optional software frame limiter
class UnlockFPS {
public:
    // Static member variables for state
    static bool g_unlockFpsEnabled;
    static float g_fpsLimit;          // 0 = Unlimited (no software cap)
    static bool g_lowLatency;         // Frame queue 1 on the D3D11 device
    static ULONGLONG g_unlockFpsEnableTime;
    static ULONGLONG g_unlockFpsDisableTime;
    static HANDLE g_waitableObject;   // Swapchain frame-latency waitable object (un-pacing target)
    
    // Methods
    static void Initialize();
    static void OnDeviceReady(ID3D11Device* device);
    static void UpdateFPS(IDXGISwapChain* pSwapChain);
    static void PreparePresent(UINT& SyncInterval, UINT& Flags);
    static void RenderArrayList(struct ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd);
    static void RenderMenu();
    static void SetFPS(float fps);
    static float GetFPS();

private:
    static ID3D11Device* g_device;
    static IDXGISwapChain3* g_swapChain3;
    static IDXGISwapChain* g_cachedChain;
    static UINT g_originalLatency;
    static bool g_originalLatencyValid;
    static void ApplyLatency(bool low);
    static void AcquireSwapChain(IDXGISwapChain* pSwapChain);
};
