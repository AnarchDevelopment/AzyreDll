#include "Render/DX11.hpp"

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
#include <imgui_internal.h>
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

// ---------------------------------------------------------------------------
// Backdrop acrilico (WinUI): captura el frame del juego y lo desenfoca con
// dos pases de downscale bilinear (1/4 y 1/16), reutilizando el renderer de
// ImGui para dibujar los quads sin escribir shaders propios.
// ---------------------------------------------------------------------------

static ID3D11Texture2D* g_blurTexA = nullptr;
static ID3D11RenderTargetView* g_blurRtvA = nullptr;
static ID3D11ShaderResourceView* g_blurSrvA = nullptr;
static UINT g_blurWA = 0, g_blurHA = 0;

static ID3D11Texture2D* g_blurTexB = nullptr;
static ID3D11RenderTargetView* g_blurRtvB = nullptr;
static ID3D11ShaderResourceView* g_blurSrvB = nullptr;
static UINT g_blurWB = 0, g_blurHB = 0;

static bool g_backdropBroken = false;

static void releaseBackdrop()
{
    if (g_blurSrvA) { g_blurSrvA->Release(); g_blurSrvA = nullptr; }
    if (g_blurRtvA) { g_blurRtvA->Release(); g_blurRtvA = nullptr; }
    if (g_blurTexA) { g_blurTexA->Release(); g_blurTexA = nullptr; }
    g_blurWA = g_blurHA = 0;

    if (g_blurSrvB) { g_blurSrvB->Release(); g_blurSrvB = nullptr; }
    if (g_blurRtvB) { g_blurRtvB->Release(); g_blurRtvB = nullptr; }
    if (g_blurTexB) { g_blurTexB->Release(); g_blurTexB = nullptr; }
    g_blurWB = g_blurHB = 0;
}

static bool createBlurTex(UINT w, UINT h, DXGI_FORMAT format, ID3D11Texture2D** tex,
                          ID3D11RenderTargetView** rtv, ID3D11ShaderResourceView** srv)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    if (FAILED(g_pd3dDevice->CreateTexture2D(&desc, nullptr, tex)))
        return false;
    if (FAILED(g_pd3dDevice->CreateRenderTargetView(*tex, nullptr, rtv)))
    {
        (*tex)->Release();
        *tex = nullptr;
        return false;
    }
    if (FAILED(g_pd3dDevice->CreateShaderResourceView(*tex, nullptr, srv)))
    {
        (*rtv)->Release();
        *rtv = nullptr;
        (*tex)->Release();
        *tex = nullptr;
        return false;
    }
    return true;
}

static bool ensureBackdrop()
{
    if (!g_pd3dDevice || !g_pSwapChain || g_DisplayWidth <= 0.0f || g_DisplayHeight <= 0.0f)
        return false;

    UINT w = (UINT)g_DisplayWidth;
    UINT h = (UINT)g_DisplayHeight;
    UINT wA = (UINT)ImMax(1.0f, g_DisplayWidth * 0.25f);
    UINT hA = (UINT)ImMax(1.0f, g_DisplayHeight * 0.25f);
    UINT wB = (UINT)ImMax(1.0f, g_DisplayWidth * 0.125f);
    UINT hB = (UINT)ImMax(1.0f, g_DisplayHeight * 0.125f);

    if (g_blurTexA && wA == g_blurWA && hA == g_blurHA &&
        g_blurTexB && wB == g_blurWB && hB == g_blurHB)
        return true;

    releaseBackdrop();

    // Formato del blur = formato del backbuffer (evita oscurecimiento si el
    // juego corre HDR con formatos float/10bit).
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    {
        ID3D11Texture2D* bb = nullptr;
        if (SUCCEEDED(g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)))
        {
            D3D11_TEXTURE2D_DESC bd = {};
            bb->GetDesc(&bd);
            fmt = bd.Format;
            bb->Release();
        }
    }
    if (fmt == DXGI_FORMAT_UNKNOWN)
        fmt = DXGI_FORMAT_B8G8R8A8_UNORM;

    if (!createBlurTex(wA, hA, fmt, &g_blurTexA, &g_blurRtvA, &g_blurSrvA))
        return false;
    g_blurWA = wA;
    g_blurHA = hA;

    if (!createBlurTex(wB, hB, fmt, &g_blurTexB, &g_blurRtvB, &g_blurSrvB))
    {
        releaseBackdrop();
        return false;
    }
    g_blurWB = wB;
    g_blurHB = hB;
    return true;
}

static void renderQuadToCurrentRT(ID3D11ShaderResourceView* srv, UINT w, UINT h)
{
    if (!srv || w == 0 || h == 0)
        return;

    ImDrawList* dl = IM_NEW(ImDrawList)(ImGui::GetDrawListSharedData());
    dl->PushClipRectFullScreen();
    dl->AddImage(ImTextureRef((void*)srv), ImVec2(0.0f, 0.0f), ImVec2((float)w, (float)h));
    dl->PopClipRect();

    ImDrawData dd;
    dd.Valid = true;
    dd.FrameCount = ImGui::GetFrameCount();
    dd.TotalIdxCount = dl->IdxBuffer.Size;
    dd.TotalVtxCount = dl->VtxBuffer.Size;
    dd.CmdLists.push_back(dl);
    dd.DisplayPos = ImVec2(0.0f, 0.0f);
    dd.DisplaySize = ImVec2((float)w, (float)h);
    dd.FramebufferScale = ImVec2(1.0f, 1.0f);
    dd.OwnerViewport = ImGui::GetMainViewport();
#ifndef IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    dd.CmdListsCount = 1;
#endif

    ImGui_ImplDX11_RenderDrawData(&dd);

    dd.CmdLists.clear();
    IM_DELETE(dl);
}

static void backdropPassBody()
{
    if (!g_pd3dDeviceContext || !g_pSwapChain)
        return;
    if (!ensureBackdrop())
        return;

    g_pd3dDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);

    // SRV efimero del backbuffer: existe SOLO durante este pase. Asi no queda
    // ninguna referencia a la swapchain al cerrar el menu (eso congelaba la
    // camara). CopyResource se evita: crashea con buffers flip-model.
    ID3D11Texture2D* bb = nullptr;
    ID3D11ShaderResourceView* bbSrv = nullptr;
    if (FAILED(g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)))
        return;
    HRESULT hr = g_pd3dDevice->CreateShaderResourceView(bb, nullptr, &bbSrv);
    bb->Release();
    if (FAILED(hr) || !bbSrv)
        return;

    // Paso 1: backbuffer -> A (1/4)
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_blurRtvA, nullptr);
    renderQuadToCurrentRT(bbSrv, g_blurWA, g_blurHA);

    // Paso 2: A -> B (1/16, blur final)
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_blurRtvB, nullptr);
    renderQuadToCurrentRT(g_blurSrvA, g_blurWB, g_blurHB);

    g_pd3dDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);

    ID3D11ShaderResourceView* nullSrv = nullptr;
    g_pd3dDeviceContext->PSSetShaderResources(0, 1, &nullSrv);
    bbSrv->Release();
}

static void runBackdropPass()
{
    if (g_backdropBroken)
        return;
    __try
    {
        backdropPassBody();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_backdropBroken = true;
        g_pd3dDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
        MC_LOG_ERROR("[Backdrop] Excepcion durante el pase de blur - blur desactivado");
    }
}

bool backdropWanted()
{
    if (mc::menu::visible())
        return true;

    const auto& notifs = mc::ModuleManager::get().notifications();
    if (!notifs.empty())
    {
        ULONGLONG now = GetTickCount64();
        for (const auto& n : notifs)
            if (now - n.time < 2600ull)
                return true;
    }

    // Modulos con panel acrilico: blur en vivo durante el juego (throttled).
    auto modOn = [](const char* name) {
        mc::Module* m = mc::ModuleManager::get().find(name);
        return m && m->enabled();
    };
    return modOn("HUD") || modOn("ArrayList") || modOn("Watermark");
}

bool backdropReady()
{
    return g_Initialized && ensureBackdrop();
}

void* backdropSrv()
{
    return (void*)g_blurSrvB;
}

static HRESULT __stdcall HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
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
            io.ConfigDebugHighlightIdConflicts = false;
            io.DisplaySize = ImVec2(g_DisplayWidth, g_DisplayHeight);

            ImFont* roboto = LoadRobotoFont(io.Fonts);
            if (roboto)
                io.FontDefault = roboto;

            ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

            g_Initialized = true;
            input::init();
            ModuleManager::get().notifyInfo("Azyre | 1.1.0 injected - INSERT to open");
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

        // Blur del frame del juego cuando algo lo usa. Con el menu abierto va
        // a full rate; en juego se limita a ~30Hz (el blur es 1/16 de resolucion,
        // la diferencia es imperceptible y la exposicion con la swapchain minima).
        if (backdropWanted())
        {
            static ULONGLONG lastPassMs = 0;
            ULONGLONG nowMs = GetTickCount64();
            if (mc::menu::visible() || nowMs - lastPassMs >= 33ull)
            {
                runBackdropPass();
                lastPassMs = nowMs;
            }
        }

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

    releaseBackdrop();

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
    releaseBackdrop();

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
