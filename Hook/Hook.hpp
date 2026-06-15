/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>


// Forward declarations
struct IDXGISwapChain;

/// @brief Hook class - Manages all hooking operations with MinHook
class Hook {
public:
    // Initialization
    static void Initialize();
    static void Shutdown();
    
    // DX11 hook callbacks
    static HRESULT STDMETHODCALLTYPE hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
    static HRESULT STDMETHODCALLTYPE hkResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
    
    // Cursor hooks
    static BOOL WINAPI hkSetCursorPos(int x, int y);
    static BOOL WINAPI hkClipCursor(const RECT* lpRect);
    
    // Original function pointers
    static HRESULT(STDMETHODCALLTYPE* oPresent)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
    static HRESULT(STDMETHODCALLTYPE* oResizeBuffers)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
    static BOOL(WINAPI* oSetCursorPos)(int x, int y);
    static BOOL(WINAPI* oClipCursor)(const RECT* lpRect);
};
