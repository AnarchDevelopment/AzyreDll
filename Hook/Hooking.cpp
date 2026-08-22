/*
Under an4rch Development Public Source License 1.0
*/

#include "Hook.hpp"
#include "../Modules/Info/Info.hpp"
#include "../Modules/Visuals/ClickGUI/ClickGUI.hpp"
#include "../Modules/Visuals/PingCounter/PingCounter.hpp"
#include "../Modules/Visuals/PlayerInfo/PlayerInfo.hpp"
#include "../Modules/Misc/UnlockFPS/UnlockFPS.hpp"
#include "../Modules/Movement/Timer/Timer.hpp"
#include "../Modules/Movement/Glide/Glide.hpp"
#include "../Modules/Movement/Fly/Fly.hpp"
#include "../Modules/Splash/Splash.hpp"
#include "../Networking/Client/IRCClient.hpp"
#include "../Core/Present.hpp"
#include "../Input/Input.hpp"
#include "../minhook/MinHook.h"
#include "../ImGui/imgui.h"
#include "../ImGui/backend/imgui_impl_dx11.h"
#include "../ImGui/backend/imgui_impl_win32.h"

// Static member definitions
HRESULT(STDMETHODCALLTYPE* Hook::oPresent)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) = NULL;
HRESULT(STDMETHODCALLTYPE* Hook::oResizeBuffers)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) = NULL;
BOOL(WINAPI* Hook::oSetCursorPos)(int x, int y) = NULL;
BOOL(WINAPI* Hook::oClipCursor)(const RECT* lpRect) = NULL;
HCURSOR(WINAPI* Hook::oSetCursor)(HCURSOR hCursor) = NULL;
int(WINAPI* Hook::oShowCursor)(BOOL bShow) = NULL;
HRESULT(WINAPI* Hook::oDwmFlush)(void) = NULL;
void(WINAPI* Hook::oSleep)(DWORD dwMilliseconds) = NULL;
DWORD(WINAPI* Hook::oWaitForSingleObjectEx)(HANDLE hObject, DWORD dwMilliseconds, BOOL bAlertable) = NULL;
NTSTATUS(NTAPI* Hook::oNtDelayExecution)(BOOLEAN Alertable, PLARGE_INTEGER DelayInterval) = NULL;

// External references from dllmain.cpp
extern bool g_showMenu;
extern bool g_wasInWorld;
extern void CleanupRenderTarget();

// Helper to get VTable address
void* GetVTableAddress(int index) {
    DXGI_SWAP_CHAIN_DESC sd = { 0 };
    sd.BufferCount = 1; 
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; 
    sd.OutputWindow = GetForegroundWindow();
    sd.SampleDesc.Count = 1; 
    sd.Windowed = TRUE; 
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    
    ID3D11Device* d; 
    ID3D11DeviceContext* c; 
    IDXGISwapChain* s;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    
    if (FAILED(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &fl, 1, D3D11_SDK_VERSION, &sd, &s, &d, NULL, &c))) 
        return 0;
    
    void* a = (*(void***)s)[index];
    s->Release(); 
    d->Release(); 
    c->Release(); 
    return a;
}

void Hook::Initialize() {
    Sleep(2000);
    MH_Initialize();
    
    // Get VTable addresses
    void* pPres = GetVTableAddress(8);
    void* pRes = GetVTableAddress(13);
    void* pSetCP = (void*)GetProcAddress(GetModuleHandleA("user32.dll"), "SetCursorPos");
    void* pClipC = (void*)GetProcAddress(GetModuleHandleA("user32.dll"), "ClipCursor");
    void* pSetCur = (void*)GetProcAddress(GetModuleHandleA("user32.dll"), "SetCursor");
    void* pShowCur = (void*)GetProcAddress(GetModuleHandleA("user32.dll"), "ShowCursor");
    void* pDwmFlush = (void*)GetProcAddress(GetModuleHandleA("dwmapi.dll"), "DwmFlush");
    void* pSleep = (void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "Sleep");
    void* pWSOEx = (void*)GetProcAddress(GetModuleHandleA("kernel32.dll"), "WaitForSingleObjectEx");
    void* pDelay = (void*)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtDelayExecution");
    
    // Create hooks
    MH_CreateHook(pPres, (LPVOID)Hook::hkPresent, (LPVOID*)&Hook::oPresent);
    MH_CreateHook(pRes, (LPVOID)Hook::hkResizeBuffers, (LPVOID*)&Hook::oResizeBuffers);
    MH_CreateHook(pSetCP, (LPVOID)Hook::hkSetCursorPos, (LPVOID*)&Hook::oSetCursorPos);
    MH_CreateHook(pClipC, (LPVOID)Hook::hkClipCursor, (LPVOID*)&Hook::oClipCursor);
    MH_CreateHook(pSetCur, (LPVOID)Hook::hkSetCursor, (LPVOID*)&Hook::oSetCursor);
    MH_CreateHook(pShowCur, (LPVOID)Hook::hkShowCursor, (LPVOID*)&Hook::oShowCursor);
    MH_CreateHook(pDwmFlush, (LPVOID)Hook::hkDwmFlush, (LPVOID*)&Hook::oDwmFlush);
    MH_CreateHook(pSleep, (LPVOID)Hook::hkSleep, (LPVOID*)&Hook::oSleep);
    MH_CreateHook(pWSOEx, (LPVOID)Hook::hkWaitForSingleObjectEx, (LPVOID*)&Hook::oWaitForSingleObjectEx);
    MH_CreateHook(pDelay, (LPVOID)Hook::hkNtDelayExecution, (LPVOID*)&Hook::oNtDelayExecution);
    
    // Enable all hooks
    MH_EnableHook(MH_ALL_HOOKS);
}

// External unload flag
extern bool g_RequestUnload;
extern HMODULE g_hModule;
extern ID3D11Device* pDevice;
extern ID3D11DeviceContext* pContext;
extern ID3D11RenderTargetView* mainRenderTargetView;

// Thread de unload separado para evitar deadlocks
static DWORD WINAPI UnloadThread(LPVOID lpParam) {
    // Esperamos para estar completamente fuera del contexto de renderizado
    Sleep(500);

    // CRITICAL: restore every hooked function BEFORE touching anything else.
    // The game's render thread calls hkPresent every frame, i.e. it is still
    // executing DLL code. Disabling all hooks makes the next Present() call go
    // straight to the original function, so no game thread can enter DLL code
    // again after this point.
    MH_DisableHook(MH_ALL_HOOKS);

    // Let any hook body already in flight on other threads finish before we
    // start freeing the resources they may still reference.
    Sleep(300);

    // Shutdown Info module
    Info::Shutdown();
    ClickGUI::ShutdownBlurShaders();
    PingCounter::Shutdown();
    PlayerInfo::Shutdown();
    Glide::Shutdown();                                   // Restore original memory
    Fly::Shutdown();                                     // Restore original memory
    Splash::Shutdown();                                  // Release splash texture

    // Stop background threads that would crash after the module is unloaded
    Timer::Disable();                                    // Timer memory-write thread
    IRCClient::GetInstance().Disconnect(false);          // IRC read thread (if connected)

    // ImGui cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    // DirectX cleanup
    if (mainRenderTargetView) {
        mainRenderTargetView->Release();
        mainRenderTargetView = nullptr;
    }

    if (pContext) {
        pContext->Release();
        pContext = nullptr;
    }

    if (pDevice) {
        pDevice->Release();
        pDevice = nullptr;
    }

    // Stop the keyboard hook thread before the module is unloaded
    Input::StopKeyboardHook();

    // NOTE: we intentionally do NOT call MH_RemoveHook()/MH_Uninitialize().
    // MinHook trampolines are VirtualAlloc'ed outside the DLL and a game thread
    // blocked inside one (Sleep/WaitForSingleObjectEx) would crash if that
    // memory were freed. Since the hooks are disabled the trampolines are dead
    // code anyway; leaking them on unload is safe and crash-free.

    // Unload DLL
    FreeLibraryAndExitThread(g_hModule, 0);
    return 0;
}

HRESULT STDMETHODCALLTYPE Hook::hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    // If unload requested, skip rendering and start cleanup thread
    if (g_RequestUnload) {
        // Create cleanup thread once
        static bool unloadStarted = false;
        if (!unloadStarted) {
            unloadStarted = true;
            HANDLE hThread = CreateThread(NULL, 0, UnloadThread, NULL, 0, NULL);
            if (hThread) {
                CloseHandle(hThread);
            }
        }
        // Just return without rendering
        return Hook::oPresent(pSwapChain, SyncInterval, Flags);
    }

    return Present::Run(pSwapChain, SyncInterval, Flags);
}

HRESULT Hook::Present(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    HRESULT hr = oPresent(pSwapChain, SyncInterval, Flags);
    if (FAILED(hr) && (Flags & 0x0200)) {
        // Fallback: Try without ALLOW_TEARING if it failed
        Flags &= ~0x0200;
        hr = oPresent(pSwapChain, SyncInterval, Flags);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE Hook::hkResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    // Guard: if unload requested, skip processing
    if (g_RequestUnload)
        return Hook::oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

    CleanupRenderTarget();
    // Reset window dimensions
    extern float g_lastW, g_lastH;
    g_lastW = 0;
    g_lastH = 0;

    // Force allow tearing for UWP FPS unlock (0x800 = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING)
    SwapChainFlags |= 0x800;

    return Hook::oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

BOOL WINAPI Hook::hkSetCursorPos(int x, int y) {
    if (g_showMenu) return TRUE;
    return Hook::oSetCursorPos(x, y);
}

BOOL WINAPI Hook::hkClipCursor(const RECT* lpRect) {
    if (g_showMenu) return Hook::oClipCursor(NULL);
    return Hook::oClipCursor(lpRect);
}

// The menu now uses ImGui's software cursor, so SetCursor/ShowCursor are passed
// through untouched: the game may keep the OS cursor hidden and ImGui draws its
// own in-world cursor when io.MouseDrawCursor is on.
HCURSOR WINAPI Hook::hkSetCursor(HCURSOR hCursor) {
    return Hook::oSetCursor(hCursor);
}

int WINAPI Hook::hkShowCursor(BOOL bShow) {
    return Hook::oShowCursor(bShow);
}

HRESULT WINAPI Hook::hkDwmFlush(void) {
    if (UnlockFPS::g_unlockFpsEnabled) {
        return S_OK;
    }
    return Hook::oDwmFlush();
}

// The engine's frame limiter usually boils down to a frame-budget sleep in the
// render loop. While unlocked, clamp short sleeps to ~1ms so the game can render
// as fast as the GPU allows. Only short (<50ms) finite timeouts are clamped to
// avoid touching real event waits (input, audio, network, INFINITE waits).
static bool ShouldClampWait(DWORD dwMilliseconds) {
    return UnlockFPS::g_unlockFpsEnabled
        && dwMilliseconds != INFINITE
        && dwMilliseconds > 1
        && dwMilliseconds <= 50;
}

void WINAPI Hook::hkSleep(DWORD dwMilliseconds) {
    if (ShouldClampWait(dwMilliseconds)) {
        dwMilliseconds = 1;
    }
    Hook::oSleep(dwMilliseconds);
}

DWORD WINAPI Hook::hkWaitForSingleObjectEx(HANDLE hObject, DWORD dwMilliseconds, BOOL bAlertable) {
    if (ShouldClampWait(dwMilliseconds)) {
        dwMilliseconds = 1;
    }
    return Hook::oWaitForSingleObjectEx(hObject, dwMilliseconds, bAlertable);
}

NTSTATUS NTAPI Hook::hkNtDelayExecution(BOOLEAN Alertable, PLARGE_INTEGER DelayInterval) {
    if (UnlockFPS::g_unlockFpsEnabled && DelayInterval && DelayInterval->QuadPart != 0) {
        LONGLONG mag = DelayInterval->QuadPart < 0 ? -DelayInterval->QuadPart : DelayInterval->QuadPart;
        if (mag > 10000) { // > 1ms in 100ns units
            static LARGE_INTEGER oneMs = { 0 };
            oneMs.QuadPart = DelayInterval->QuadPart < 0 ? -10000 : 10000;
            DelayInterval = &oneMs;
        }
    }
    return Hook::oNtDelayExecution(Alertable, DelayInterval);
}

void Hook::Shutdown() {
    // Desactivar y remover hooks individuales (más seguro)
    if (oPresent) {
        MH_DisableHook(oPresent);
        MH_RemoveHook(oPresent);
        oPresent = NULL;
    }

    if (oResizeBuffers) {
        MH_DisableHook(oResizeBuffers);
        MH_RemoveHook(oResizeBuffers);
        oResizeBuffers = NULL;
    }

    if (oSetCursorPos) {
        MH_DisableHook(oSetCursorPos);
        MH_RemoveHook(oSetCursorPos);
        oSetCursorPos = NULL;
    }

    if (oClipCursor) {
        MH_DisableHook(oClipCursor);
        MH_RemoveHook(oClipCursor);
        oClipCursor = NULL;
    }

    if (oSetCursor) {
        MH_DisableHook(oSetCursor);
        MH_RemoveHook(oSetCursor);
        oSetCursor = NULL;
    }

    if (oShowCursor) {
        MH_DisableHook(oShowCursor);
        MH_RemoveHook(oShowCursor);
        oShowCursor = NULL;
    }

    if (oDwmFlush) {
        MH_DisableHook(oDwmFlush);
        MH_RemoveHook(oDwmFlush);
        oDwmFlush = NULL;
    }

    if (oSleep) {
        MH_DisableHook(oSleep);
        MH_RemoveHook(oSleep);
        oSleep = NULL;
    }

    if (oWaitForSingleObjectEx) {
        MH_DisableHook(oWaitForSingleObjectEx);
        MH_RemoveHook(oWaitForSingleObjectEx);
        oWaitForSingleObjectEx = NULL;
    }

    if (oNtDelayExecution) {
        MH_DisableHook(oNtDelayExecution);
        MH_RemoveHook(oNtDelayExecution);
        oNtDelayExecution = NULL;
    }

    // Finalmente apagar MinHook
    MH_Uninitialize();
}
