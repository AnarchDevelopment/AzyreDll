/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <windows.h>
#include <winternl.h>
#include <d3d11.h>
#include <dxgi.h>


// Forward declarations
struct IDXGISwapChain;

// NtDelayExecution is not declared in the SDK headers; declare it manually.
extern "C" NTSTATUS NTAPI NtDelayExecution(BOOLEAN Alertable, PLARGE_INTEGER DelayInterval);

/*


NTSTATUS NtDelayExecution(BOOLEAN Alertable, PLARGE_INTEGER DelayInterval)
{
return NTSTATUS();
}


*/

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
    static HCURSOR WINAPI hkSetCursor(HCURSOR hCursor);
    static int WINAPI hkShowCursor(BOOL bShow);
    
    // Frame pacing hooks (UWP FPS unlock)
    static HRESULT WINAPI hkDwmFlush(void);
    static void WINAPI hkSleep(DWORD dwMilliseconds);
    static DWORD WINAPI hkWaitForSingleObjectEx(HANDLE hObject, DWORD dwMilliseconds, BOOL bAlertable);
    static NTSTATUS NTAPI hkNtDelayExecution(BOOLEAN Alertable, PLARGE_INTEGER DelayInterval);
    
    // Original function pointers
    static HRESULT(STDMETHODCALLTYPE* oPresent)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);

    // Present helper with ALLOW_TEARING fallback
    static HRESULT Present(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
    static HRESULT(STDMETHODCALLTYPE* oResizeBuffers)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
    static BOOL(WINAPI* oSetCursorPos)(int x, int y);
    static BOOL(WINAPI* oClipCursor)(const RECT* lpRect);
    static HCURSOR(WINAPI* oSetCursor)(HCURSOR hCursor);
    static int(WINAPI* oShowCursor)(BOOL bShow);
    static HRESULT(WINAPI* oDwmFlush)(void);
    static void(WINAPI* oSleep)(DWORD dwMilliseconds);
    static DWORD(WINAPI* oWaitForSingleObjectEx)(HANDLE hObject, DWORD dwMilliseconds, BOOL bAlertable);
    static NTSTATUS(NTAPI* oNtDelayExecution)(BOOLEAN Alertable, PLARGE_INTEGER DelayInterval);
};
