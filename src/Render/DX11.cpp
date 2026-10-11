#include "Render/DX11.hpp"

#include "Config/Config.hpp"
#include "Framework/Hook.hpp"
#include "Framework/Log.hpp"
#include "Features/ECS/ECS.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Theme.hpp"
#include "Input/InputSystem.hpp"
#include "Modules/ModuleManager.hpp"
#include "Render/BlurHelper/BlurHelper.hpp"
#include "Render/EffectSprites.hpp"
#include "SDK/Game.hpp"
#include "resource.h"

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>

namespace mc::dx11 {

static HMODULE g_module = nullptr;

void setModule(void* moduleHandle)
{
    g_module = static_cast<HMODULE>(moduleHandle);
}

void* moduleHandle()
{
    return g_module;
}

static ImFont* sehLoadFont(ImFontAtlas* fonts, void* data, int size, float px)
{
    __try
    {
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        cfg.SizePixels = px;
        return fonts->AddFontFromMemoryTTF(data, size, px, &cfg);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

static ImFont* g_hudFont = nullptr;

ImFont* hudFont()
{
    return g_hudFont;
}

static void LoadAllFonts(ImFontAtlas* fonts)
{
    if (!g_module)
        return;

    struct FontEntry
    {
        int id;
        const char* name;
    };

    static const FontEntry entries[] = {
        {IDR_ROBOTO, "Roboto"},
        {IDR_COMFORTAA, "Comfortaa"},
        {IDR_COMFORTAA_BOLD, "Comfortaa-Bold"},
        {IDR_MOJANGLES, "Mojangles"},
        {IDR_MOJANGLES_BOLD, "Mojangles-Bold"},
        {IDR_OPEN_SANS, "Open-Sans"},
        {IDR_OPEN_SANS_BOLD, "Open-Sans-Bold"},
        {IDR_PRODUCT_SANS, "Product-Sans"},
        {IDR_PRODUCT_SANS_BOLD, "Product-Sans-Bold"},
        {IDR_SF_PRO, "SF-Pro"},
        {IDR_SF_PRO_BOLD, "SF-Pro-Bold"},
        {IDR_SARABUN, "Sarabun"},
    };

    for (const FontEntry& fe : entries)
    {
        HRSRC res = FindResourceW(g_module, MAKEINTRESOURCEW(fe.id), RT_RCDATA);
        if (!res)
        {
            MC_LOG_ERROR("[Font] %s: resource not found", fe.name);
            continue;
        }
        HGLOBAL hg = LoadResource(g_module, res);
        if (!hg)
        {
            MC_LOG_ERROR("[Font] %s: LoadResource failed", fe.name);
            continue;
        }
        DWORD fsize = SizeofResource(g_module, res);
        void* data = LockResource(hg);
        if (!data || !fsize)
        {
            MC_LOG_ERROR("[Font] %s: null data or size", fe.name);
            continue;
        }

        if (sehLoadFont(fonts, data, (int)fsize, 16.0f))
            MC_LOG("[Font] %s: OK (%lu bytes)", fe.name, fsize);
        else
            MC_LOG_ERROR("[Font] %s: FAILED (%lu bytes) - corrupted TTF?", fe.name, fsize);
    }

    // Copia grande del Product Sans Bold para widgets HUD (ArrayList estilo
    // Solstice): el atlas base es de 16px y escalado a ~25px se ve suave;
    // esta copia horneada a 26px se ve crujiente en ese rango.
    {
        HRSRC res = FindResourceW(g_module, MAKEINTRESOURCEW(IDR_PRODUCT_SANS_BOLD), RT_RCDATA);
        if (res)
        {
            HGLOBAL hg = LoadResource(g_module, res);
            if (hg)
            {
                DWORD fsize = SizeofResource(g_module, res);
                void* data = LockResource(hg);
                if (data && fsize)
                {
                    g_hudFont = sehLoadFont(fonts, data, (int)fsize, 26.0f);
                    if (g_hudFont)
                        MC_LOG("[Font] Product-Sans-Bold-26: OK (hud font)");
                    else
                        MC_LOG_ERROR("[Font] Product-Sans-Bold-26: FAILED");
                }
            }
        }
    }

    MC_LOG("[Font] Atlas: %zu fonts loaded", fonts->Fonts.size());
}
typedef HRESULT(__stdcall* PresentFunc)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
typedef HRESULT(__stdcall* ResizeBuffersFunc)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height,
                                              DXGI_FORMAT NewFormat, UINT SwapChainFlags);

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;

void* d3dDevice() { return g_pd3dDevice; }
void* d3dContext() { return g_pd3dDeviceContext; }
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
static void renderQuadToCurrentRT(ID3D11ShaderResourceView* srv, UINT w, UINT h)
{
    if (!srv || w == 0 || h == 0)
        return;

    ImDrawList* dl = IM_NEW(ImDrawList)(ImGui::GetDrawListSharedData());
    dl->PushClipRectFullScreen();
    dl->AddImage(ImTextureRef((void*)srv), ImVec2(0.0f, 0.0f), ImVec2((float)w, (float)h));
    dl->PopClipRect();

    ImDrawData dd{};
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

            mc::blur::init(g_pd3dDevice, g_pd3dDeviceContext, pSwapChain);
            mc::blur::setDisplaySize(g_DisplayWidth, g_DisplayHeight);

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            mc::theme::apply();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
            io.ConfigDebugHighlightIdConflicts = false;
            // Input legacy por polling (MousePos/MouseDown directos + eventos
            // de wheel): la Trickling Rule difiere el evento de wheel cada
            // vez que hay un MousePos previo en la cola (i.e. cualquier
            // micro-movimiento del mouse mientras scrolleas) y lo muere de
            // inanicion. Con polling plano no aporta nada: fuera.
            io.ConfigInputTrickleEventQueue = false;
            io.DisplaySize = ImVec2(g_DisplayWidth, g_DisplayHeight);

            LoadAllFonts(io.Fonts);
            if (!io.Fonts->Fonts.empty())
                io.FontDefault = io.Fonts->Fonts[theme::FontIndex];

            ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

            g_Initialized = true;
            input::init();
            ModuleManager::get().notifyInfo("Azyre | 1.1.0 injected - INSERT to open", 6.0f);
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

        // Diagnostico del pipeline de scroll: el evento encolado por
        // input::updateFrame debe verse aqui como io.MouseWheel != 0.
        if (ImGui::GetIO().MouseWheel != 0.0f)
        {
            static ULONGLONG lastMwDbg = 0;
            ULONGLONG nowMw = GetTickCount64();
            if (nowMw - lastMwDbg > 200ull)
            {
                lastMwDbg = nowMw;
                MC_LOG("[WheelDBG] io.MouseWheel=%.2f tras NewFrame",
                       ImGui::GetIO().MouseWheel);
            }
        }

        Game::get().update();
        ecs::get().sync(Game::get());
        ecs::get().syncBlocks(Game::get());
        ecs::get().dispatcher.update();
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
        // Los paneles acrilicos de los widgets registran sus rects este frame;
        // el BlurHelper los compone en el backbuffer (shader Glass) en runPass.
        mc::blur::beginAcrylic();
        ModuleManager::get().onRender();
        menu::handleInput();
        menu::render();
        menu::renderToasts();
        menu::renderHudChrome();

        ImGui::EndFrame();
        ImGui::Render();

        // Blur del frame del juego cuando algo lo usa. Con el menu abierto va
        // a full rate; en juego se limita a ~30Hz.
            mc::blur::setDisplaySize(g_DisplayWidth, g_DisplayHeight);
            mc::blur::identifyGameBbRTV(g_originalRenderTargetView);

            // El composite Mica debe correr a FULL RATE durante TODO el fade
            // (incluido el de cierre): con el menu visible() en false y solo
            // el throttle de 33ms, los frames alternan frost/sin-frost durante
            // la animacion de cierre = parpadeo.
            bool fadeActive = mc::menu::animationAlpha() > 0.001f;
            bool bdWanted = mc::blur::wanted() || fadeActive;
            mc::blur::setRecording(bdWanted);
            mc::blur::armProxy(bdWanted);
            if (bdWanted)
            {
                static ULONGLONG lastPassMs = 0;
                ULONGLONG nowMs = GetTickCount64();
                // MotionBlur graba a full rate: los ghosts deben ser frames
                // consecutivos o el trail queda entrecortado.
                if (mc::menu::visible() || fadeActive || mc::blur::ghostRecording() ||
                    nowMs - lastPassMs >= 33ull)
                {
                    mc::blur::runPass(g_originalRenderTargetView);
                    lastPassMs = nowMs;
                }
            }
            else
            {
                mc::blur::clearRing();
            }

        // Con el proxy activo el juego nunca dibujo en el backbuffer real:
        // volcar el frame capturado antes de renderizar el overlay.
        if (void* proxyFrame = mc::blur::takeProxyFrame())
        {
            g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
            renderQuadToCurrentRT((ID3D11ShaderResourceView*)proxyFrame,
                                  (UINT)g_DisplayWidth, (UINT)g_DisplayHeight);
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

    mc::blur::onResize();

    if (g_pd3dDeviceContext)
    {
        g_pd3dDeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
    }

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
    // Texturas propias antes de soltar el device (sprites de efectos, etc).
    mc::fxsprites::unloadAll();

    mc::blur::shutdown();

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
