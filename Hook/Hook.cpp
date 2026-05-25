/*
Under an4rch Development Public Source License 1.0
*/

#include "Hook.hpp"
#include "../Modules/Info/Info.hpp"
#include "../minhook/MinHook.h"
#include "../ImGui/imgui.h"
#include "../ImGui/backend/imgui_impl_dx11.h"
#include "../ImGui/backend/imgui_impl_win32.h"

// Static member definitions
HRESULT(STDMETHODCALLTYPE* Hook::oPresent)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) = NULL;
HRESULT(STDMETHODCALLTYPE* Hook::oResizeBuffers)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) = NULL;
BOOL(WINAPI* Hook::oSetCursorPos)(int x, int y) = NULL;
BOOL(WINAPI* Hook::oClipCursor)(const RECT* lpRect) = NULL;

// External references from dllmain.cpp
extern bool g_showMenu;
extern HRESULT STDMETHODCALLTYPE hkPresent_Impl(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
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
    
    // Create hooks
    MH_CreateHook(pPres, (LPVOID)Hook::hkPresent, (LPVOID*)&Hook::oPresent);
    MH_CreateHook(pRes, (LPVOID)Hook::hkResizeBuffers, (LPVOID*)&Hook::oResizeBuffers);
    MH_CreateHook(pSetCP, (LPVOID)Hook::hkSetCursorPos, (LPVOID*)&Hook::oSetCursorPos);
    MH_CreateHook(pClipC, (LPVOID)Hook::hkClipCursor, (LPVOID*)&Hook::oClipCursor);
    
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

    // Shutdown Info module
    Info::Shutdown();

    // Hooks cleanup
    if (Hook::oPresent) {
        MH_DisableHook(Hook::oPresent);
        MH_RemoveHook(Hook::oPresent);
        Hook::oPresent = NULL;
    }

    if (Hook::oResizeBuffers) {
        MH_DisableHook(Hook::oResizeBuffers);
        MH_RemoveHook(Hook::oResizeBuffers);
        Hook::oResizeBuffers = NULL;
    }

    if (Hook::oSetCursorPos) {
        MH_DisableHook(Hook::oSetCursorPos);
        MH_RemoveHook(Hook::oSetCursorPos);
        Hook::oSetCursorPos = NULL;
    }

    if (Hook::oClipCursor) {
        MH_DisableHook(Hook::oClipCursor);
        MH_RemoveHook(Hook::oClipCursor);
        Hook::oClipCursor = NULL;
    }

    MH_Uninitialize();

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

    return hkPresent_Impl(pSwapChain, SyncInterval, Flags);
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

    // Finalmente apagar MinHook
    MH_Uninitialize();
}
