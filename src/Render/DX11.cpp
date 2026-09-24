#include "DX11.hpp"

#include "Framework/Log.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Theme.hpp"
#include "Input/InputSystem.hpp"
#include "Modules/ModuleManager.hpp"
#include "SDK/Game.hpp"
#include "resource.h"

#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>

namespace mc::dx11 {

static HMODULE g_module = nullptr;

void setModule(void* moduleHandle)
{
    g_module = static_cast<HMODULE>(moduleHandle);
}

static ImFont* LoadRobotoFont(ImFontAtlas* fonts)
{
    if (!g_module)
        return nullptr;

    HRSRC res = FindResourceW(g_module, MAKEINTRESOURCEW(IDR_ROBOTO), RT_RCDATA);
    if (!res)
    {
        MC_LOG_ERROR("[Font] Roboto resource not found");
        return nullptr;
    }

    HGLOBAL hg = LoadResource(g_module, res);
    DWORD size = SizeofResource(g_module, res);
    void* data = LockResource(hg);
    if (!data || !size)
    {
        MC_LOG_ERROR("[Font] Failed to lock Roboto resource");
        return nullptr;
    }

    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.SizePixels = 16.0f;

    ImFont* font = fonts->AddFontFromMemoryTTF(data, (int)size, 16.0f, &cfg);
    if (!font)
    {
        MC_LOG_ERROR("[Font] AddFontFromMemoryTTF failed");
        return nullptr;
    }

    MC_LOG("[Font] Roboto loaded (%lu bytes)", size);
    return font;
}

typedef HRESULT(__stdcall* PresentFunc)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
typedef HRESULT(__stdcall* ResizeBuffersFunc)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height,
                                              DXGI_FORMAT NewFormat, UINT SwapChainFlags);

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static ID3D11RenderTargetView* g_originalRenderTargetView = nullptr;
static ID3D11DepthStencilView* g_originalDepthStencilView = nullptr;
static bool g_Initialized = false;
static PresentFunc oPresent = nullptr;
static ResizeBuffersFunc oResizeBuffers = nullptr;
static void** g_pSwapChainVTable = nullptr;
static HWND g_Hwnd = nullptr;
static float g_DisplayWidth = 0;
static float g_DisplayHeight = 0;

static void CreateRenderTarget(IDXGISwapChain* pSwapChain)
{
    MC_LOG("[DX11] Creating render target view...");

    if (g_mainRenderTargetView)
    {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }

    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (FAILED(hr))
    {
        MC_LOG_ERROR("[ERROR] Failed to get back buffer: 0x%08X", hr);
        return;
    }

    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();

    if (FAILED(hr))
    {
        MC_LOG_ERROR("[ERROR] Failed to create render target view: 0x%08X", hr);
        return;
    }

    MC_LOG("[DX11] Render target view created: 0x%p", g_mainRenderTargetView);
}

static HRESULT __stdcall HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    static int callCount = 0;
    callCount++;

    if (!g_Initialized)
    {
        MC_LOG("[VTable] ========================================");
        MC_LOG("[VTable] First Present call - Initializing ImGui");
        MC_LOG("[VTable] ========================================");

        HRESULT hr = pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pd3dDevice);
        if (SUCCEEDED(hr))
        {
            g_pd3dDevice->GetImmediateContext(&g_pd3dDeviceContext);
            g_pSwapChain = pSwapChain;

            DXGI_SWAP_CHAIN_DESC desc;
            pSwapChain->GetDesc(&desc);
            g_DisplayWidth = (float)desc.BufferDesc.Width;
            g_DisplayHeight = (float)desc.BufferDesc.Height;
            g_Hwnd = desc.OutputWindow;
            MC_LOG("[VTable] Display size: %.0f x %.0f | HWND: 0x%p", g_DisplayWidth, g_DisplayHeight, g_Hwnd);

            CreateRenderTarget(pSwapChain);

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            mc::theme::apply();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
            io.DisplaySize = ImVec2(g_DisplayWidth, g_DisplayHeight);

            ImFont* roboto = LoadRobotoFont(io.Fonts);
            if (roboto)
                io.FontDefault = roboto;

            ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

            g_Initialized = true;
            input::init();
            ModuleManager::get().notifyInfo("Azyre | 1.1.0 inyectado — INSERT para abrir");
            MC_LOG("[SUCCESS] ImGui initialized successfully!");
        }
        else
        {
            MC_LOG_ERROR("[ERROR] Failed to get D3D11 Device: 0x%08X", hr);
        }
    }

    if (g_Initialized && g_pd3dDeviceContext)
    {
        input::updateFrame();

        g_pd3dDeviceContext->OMGetRenderTargets(1, &g_originalRenderTargetView, &g_originalDepthStencilView);

        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();

        Game::get().update();
        ModuleManager::get().handleKeybinds();

        static float tickAccum = 0.0f;
        tickAccum += ImGui::GetIO().DeltaTime;
        if (tickAccum >= 0.05f)
        {
            int steps = (int)(tickAccum / 0.05f);
            if (steps > 4)
                steps = 4;
            tickAccum -= steps * 0.05f;
            for (int i = 0; i < steps; ++i)
                ModuleManager::get().onTick();
        }

        ModuleManager::get().onFrame(ImGui::GetIO().DeltaTime);
        ModuleManager::get().onRender();
        menu::handleInput();
        menu::render();
        menu::renderToasts();

        ImGui::EndFrame();
        ImGui::Render();

        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        if (g_originalRenderTargetView)
        {
            g_pd3dDeviceContext->OMSetRenderTargets(1, &g_originalRenderTargetView, g_originalDepthStencilView);
            g_originalRenderTargetView->Release();
            g_originalRenderTargetView = nullptr;
        }
        if (g_originalDepthStencilView)
        {
            g_originalDepthStencilView->Release();
            g_originalDepthStencilView = nullptr;
        }
    }

    return oPresent(pSwapChain, SyncInterval, Flags);
}

static HRESULT __stdcall HookedResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height,
                                             DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    MC_LOG("[VTable] ResizeBuffers called - Width: %d, Height: %d", Width, Height);

    if (g_mainRenderTargetView)
    {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }

    HRESULT hr = oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

    if (g_Initialized && SUCCEEDED(hr))
    {
        CreateRenderTarget(pSwapChain);

        if (g_pd3dDeviceContext)
        {
            g_DisplayWidth = (float)Width;
            g_DisplayHeight = (float)Height;
            ImGui::GetIO().DisplaySize = ImVec2(g_DisplayWidth, g_DisplayHeight);
        }
    }

    return hr;
}

bool install()
{
    MC_LOG("[VTable] ========================================");
    MC_LOG("[VTable] Installing VTable hooks");
    MC_LOG("[VTable] ========================================");

    IDXGISwapChain* pDummySwapChain = nullptr;
    ID3D11Device* pDummyDevice = nullptr;
    ID3D11DeviceContext* pDummyContext = nullptr;

    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = 1;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.Width = 1;
    desc.BufferDesc.Height = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = GetDesktopWindow();
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Windowed = TRUE;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &desc,
        &pDummySwapChain,
        &pDummyDevice,
        nullptr,
        &pDummyContext);

    if (FAILED(hr) || !pDummySwapChain)
    {
        MC_LOG_ERROR("[ERROR] Failed to create dummy swapchain: 0x%08X", hr);
        return false;
    }

    void** pVTable = *(void***)pDummySwapChain;
    oPresent = (PresentFunc)pVTable[8];
    oResizeBuffers = (ResizeBuffersFunc)pVTable[13];

    MC_LOG("[VTable] Original Present at: 0x%p", oPresent);
    MC_LOG("[VTable] Original ResizeBuffers at: 0x%p", oResizeBuffers);

    g_pSwapChainVTable = pVTable;

    DWORD oldProtect;
    VirtualProtect(&pVTable[8], sizeof(void*), PAGE_READWRITE, &oldProtect);
    pVTable[8] = (void*)HookedPresent;
    VirtualProtect(&pVTable[8], sizeof(void*), oldProtect, &oldProtect);

    VirtualProtect(&pVTable[13], sizeof(void*), PAGE_READWRITE, &oldProtect);
    pVTable[13] = (void*)HookedResizeBuffers;
    VirtualProtect(&pVTable[13], sizeof(void*), oldProtect, &oldProtect);

    pDummySwapChain->Release();
    pDummyDevice->Release();
    pDummyContext->Release();

    MC_LOG("[SUCCESS] VTable hooks installed successfully!");
    return true;
}

void shutdown()
{
    if (g_pSwapChainVTable)
    {
        MC_LOG("[VTable] Restoring original VTable...");
        DWORD oldProtect;
        VirtualProtect(&g_pSwapChainVTable[8], sizeof(void*), PAGE_READWRITE, &oldProtect);
        g_pSwapChainVTable[8] = (void*)oPresent;
        VirtualProtect(&g_pSwapChainVTable[8], sizeof(void*), oldProtect, &oldProtect);

        VirtualProtect(&g_pSwapChainVTable[13], sizeof(void*), PAGE_READWRITE, &oldProtect);
        g_pSwapChainVTable[13] = (void*)oResizeBuffers;
        VirtualProtect(&g_pSwapChainVTable[13], sizeof(void*), oldProtect, &oldProtect);
        g_pSwapChainVTable = nullptr;
    }

    if (g_Initialized)
    {
        MC_LOG("[DX11] Shutting down ImGui");
        ImGui_ImplDX11_Shutdown();
        ImGui::DestroyContext();
        g_Initialized = false;
    }

    if (g_mainRenderTargetView)
    {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
    if (g_pd3dDeviceContext)
    {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice)
    {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
    if (g_pSwapChain)
    {
        g_pSwapChain = nullptr;
    }
}

}
