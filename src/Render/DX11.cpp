#include "Render/DX11.hpp"

#include "Config/Config.hpp"
#include "Framework/Hook.hpp"
#include "Framework/Log.hpp"
#include "Features/ECS/ECS.hpp"
#include "Features/Rotation.hpp"
#include "Features/RotationHelper.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Theme.hpp"
#include "Input/InputSystem.hpp"
#include "Modules/ModuleManager.hpp"
#include "Modules/impl/Interface/Interface.hpp"
#include "Render/BlurHelper/BlurHelper.hpp"
#include "Render/EffectSprites.hpp"
#include "Render/ResourceSprites.hpp"
#include "Render/D2D/D2D.hpp"
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

#include <atomic>

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
static ImFont* g_gameFont = nullptr;
static ImFont* g_riseFont = nullptr;

ImFont* hudFont()
{
    return g_hudFont;
}

// Mojangles (fuente Minecraft) — la usa el PlayerList (TAB).
ImFont* gameFont()
{
    return g_gameFont;
}

// Product Sans regular — la usa el estilo Rise (como Rise real).
ImFont* riseFont()
{
    return g_riseFont;
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

        ImFont* loaded = sehLoadFont(fonts, data, (int)fsize, 16.0f);
        if (loaded)
        {
            if (fe.id == IDR_MOJANGLES)
                g_gameFont = loaded;
            MC_LOG("[Font] %s: OK (%lu bytes)", fe.name, fsize);
        }
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

    // Product Sans regular a 16px — la usa el estilo Rise (font oficial de Rise).
    {
        HRSRC res = FindResourceW(g_module, MAKEINTRESOURCEW(IDR_PRODUCT_SANS), RT_RCDATA);
        if (res)
        {
            HGLOBAL hg = LoadResource(g_module, res);
            if (hg)
            {
                DWORD fsize = SizeofResource(g_module, res);
                void* data = LockResource(hg);
                if (data && fsize)
                {
                    g_riseFont = sehLoadFont(fonts, data, (int)fsize, 16.0f);
                    if (g_riseFont)
                        MC_LOG("[Font] Product-Sans-16: OK (rise font)");
                    else
                        MC_LOG_ERROR("[Font] Product-Sans-16: FAILED");
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

// --- Unlock FPS (DXGI) ---
static std::atomic<bool> g_unlockFps{false};
static std::atomic<bool> g_allowTearing{true};

void setUnlockFps(bool v) { g_unlockFps.store(v, std::memory_order_relaxed); }
bool unlockFps() { return g_unlockFps.load(std::memory_order_relaxed); }
void setAllowTearing(bool v) { g_allowTearing.store(v, std::memory_order_relaxed); }
bool allowTearing() { return g_allowTearing.load(std::memory_order_relaxed); }

float swapchainRefreshHz()
{
    if (!g_pSwapChain)
        return 0.0f;
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(g_pSwapChain->GetDesc(&desc)))
        return 0.0f;
    if (desc.BufferDesc.RefreshRate.Denominator == 0)
        return 0.0f;
    float hz = (float)desc.BufferDesc.RefreshRate.Numerator /
               (float)desc.BufferDesc.RefreshRate.Denominator;
    // En swapchains de composicion (UWP XAML) el refresh del desc suele
    // venir 0/0 o un valor sentinela: solo se reporta si es creible.
    if (hz < 20.0f || hz > 500.0f)
        return 0.0f;
    return hz;
}

bool swapchainTearingSupported()
{
    if (!g_pSwapChain)
        return false;
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(g_pSwapChain->GetDesc(&desc)))
        return false;
    return (desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0;
}

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

// Cursor del sistema visible con nuestro menu cerrado y la ventana del
// juego enfocada = una pantalla del JUEGO abierta (inventario, contenedor,
// chat, pause): el frame se presenta por el path de composicion XAML. Con
// el foco en otra ventana (alt-tab) el cursor tambien es visible pero no
// hay pantalla del juego — no hay que congelar nada.
//
// Solo aplica DENTRO de un mundo (localFound): en el menu principal del
// juego la captura completa del backbuffer es la via probada (los logs
// "Clone capture ok" nacen ahi) y es la unica fuente de blur para los
// widgets — sin esto el blur solo existia "donde hay world".
static bool gameScreenOpen()
{
    if (!Game::get().localFound())
        return false; // menus del juego sin mundo: pipeline completo
    if (mc::menu::visible() || mc::menu::hudEditing())
        return false;
    if (g_Hwnd && GetForegroundWindow() != g_Hwnd)
        return false;
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci))
        return true;
    return (ci.flags & CURSOR_SHOWING) != 0;
}

// Overlay envenenado: la recuperacion de la guardia fallo tambien — el hook
// queda en passthrough limpio (sin re-init en cascada), el juego sigue vivo.
static bool g_overlayDead = false;

static void presentBody(IDXGISwapChain* pSwapChain)
{
    if (g_overlayDead)
        return;

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
        // Advance the shared local model-rotation helper every Present. Its
        // lifetime must not depend on Scaffold being enabled: KillAura and
        // other combat modules use the same render-scoped model hook.
        rotation_helper::updateClientRotation();

        // NOTA (rotaciones visuales): se probo escribir el par de render
        // (+0x44/+0x48) para girar solo el modelo — MUEVE LA CAMARA: en
        // este build camara y modelo comparten el render rot (no hay un
        // campo separado en +0x38..+0x7C). La unica aplicacion segura es
        // el hook del MovePlayerPacket (ChatHook::applyRotation) —
        // rotacion 100% server-side, igual que Solstice.

        // Los paneles acrilicos de los widgets registran sus rects este frame;
        // el BlurHelper los compone en el backbuffer (shader Glass) en runPass.
        mc::blur::beginAcrylic();
        ModuleManager::get().onRender();
        menu::handleInput();
        menu::render();
        menu::renderHudChrome();

        // ImGui mantiene un LOCK de wheeling (g.WheelingWindow): la ventana
        // que lo toma se queda con TODOS los eventos de wheel hasta que el
        // mouse se mueve de golpe o venza el timer (~16s). Si esa ventana
        // dejo de existir (cambio de estilo de GUI, drawer cerrado, child
        // reciclado), sigue "scrolleando en la nada" y se come el wheel de
        // TODA la GUI — marcaba WheelingWindowScrolledFrame cada frame y los
        // scrolls custom se saltaban solos. Soltar locks muertos: a estas
        // alturas del frame, toda ventana viva ya hizo Begin/End.
        if (GImGui && GImGui->WheelingWindow &&
            GImGui->WheelingWindow->LastFrameActive != ImGui::GetFrameCount())
        {
            GImGui->WheelingWindow = nullptr;
            GImGui->WheelingWindowStartFrame = -1;
            GImGui->WheelingWindowScrolledFrame = -1;
            GImGui->WheelingWindowReleaseTimer = 0.0f;
            GImGui->WheelingWindowWheelRemainder = ImVec2(0.0f, 0.0f);
        }

        ImGui::EndFrame();
        ImGui::Render();

        // Blur del frame del juego cuando algo lo usa. Con el menu abierto va
        // a full rate; en juego se limita a ~30Hz.
            mc::blur::setDisplaySize(g_DisplayWidth, g_DisplayHeight);

            // Pantalla del juego abierta (inventario, cofre, chat, pause): el
            // frame llega por el path de composicion XAML. El pase sigue
            // corriendo (blur en vivo para los widgets) pero en modo seguro:
            // sin tocar el swap chain ni escribir el backbuffer, y sin
            // reclamar el RT de la UI como backbuffer del juego.
            bool gameScreen = gameScreenOpen();
            if (!gameScreen)
                mc::blur::identifyGameBbRTV(g_originalRenderTargetView);

            // El composite Mica debe correr a FULL RATE durante TODO el fade
            // (incluido el de cierre): con el menu visible() en false y solo
            // el throttle de 33ms, los frames alternan frost/sin-frost durante
            // la animacion de cierre = parpadeo.
            bool fadeActive = mc::menu::animationAlpha() > 0.001f;
            bool bdWanted = mc::blur::wanted() || fadeActive || mc::blur::motionBlurEnabled();
            mc::blur::setRecording(bdWanted);
            mc::blur::armProxy(bdWanted && !gameScreen);
            if (bdWanted)
            {
                static ULONGLONG lastPassMs = 0;
                ULONGLONG nowMs = GetTickCount64();
                // MotionBlur graba a full rate: los ghosts deben ser frames
                // consecutivos o el trail queda entrecortado.
                if (mc::menu::visible() || fadeActive || mc::blur::motionBlurEnabled() ||
                    mc::blur::hasAcrylic() || nowMs - lastPassMs >= 33ull)
                {
                    mc::blur::runPass(g_originalRenderTargetView, !gameScreen);
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

        // Solstice blur (D2D): SOLO en modo Solstice. En modo Native, D2D queda
        // completamente inerte (ni init ni begin/end) para no interferir con el
        // device del juego. La init es lazy: la primera vez que se usa.
#ifdef _MSC_VER
        bool d2dActive = (mc::gui::blurMode() == 1);
        if (d2dActive)
        {
            static bool s_d2dInited = false;
            if (!s_d2dInited)
            {
                D2D::init(pSwapChain, g_pd3dDevice);
                s_d2dInited = true;
            }
            IDXGISurface* d2dSurface = nullptr;
            if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(IDXGISurface), (void**)&d2dSurface)) &&
                d2dSurface)
            {
                D2D::beginRender(d2dSurface, 96.0f);
                d2dSurface->Release();
            }
        }
#endif
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
#ifdef _MSC_VER
        if (d2dActive)
            D2D::endRender();
#endif

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
}

// Guardia SEH del hook de Present: el hook corre DENTRO del path de
// composicion XAML del juego (Windows.UI.Xaml -> game -> Present), donde
// cualquier excepcion que escape cruza un boundary WinRT noexcept y mata el
// proceso con fail-fast SIN dejar log. Con la guardia, una falta = frame de
// overlay saltado + reporte [Guard][Present] con el stack del culpable;
// el juego sigue presentando (oPresent SIEMPRE se llama).
static int presentGuardFilter(EXCEPTION_POINTERS* ep)
{
    mc::log::reportGuard("Present", ep);
    return EXCEPTION_EXECUTE_HANDLER;
}

static int resizeGuardFilter(EXCEPTION_POINTERS* ep)
{
    mc::log::reportGuard("Resize", ep);
    return EXCEPTION_EXECUTE_HANDLER;
}

static HRESULT __stdcall HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    __try
    {
        presentBody(pSwapChain);
    }
    __except (presentGuardFilter(GetExceptionInformation()))
    {
        // Dejar a ImGui en estado coherente: la falta pudo ocurrir entre
        // NewFrame y EndFrame y el proximo frame debe poder arrancar.
        __try
        {
            if (ImGui::GetCurrentContext() && GImGui && GImGui->WithinFrameScope)
            {
                ImGui::EndFrame();
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_overlayDead = true;
        }
    }

    // Unlock FPS: SyncInterval=0 salta el vsync/cap del compositor. Si el
    // swapchain soporta tearing, la bandera evita que el compositor vuelva
    // a encolar el frame (DXGI_PRESENT_ALLOW_TEARING requiere que el
    // swapchain se haya creado con ALLOW_TEARING).
    UINT sync = SyncInterval;
    UINT flags = Flags;
    if (g_unlockFps.load(std::memory_order_relaxed))
    {
        sync = 0;
        if (g_allowTearing.load(std::memory_order_relaxed) && swapchainTearingSupported())
            flags |= DXGI_PRESENT_ALLOW_TEARING;
    }

    return oPresent(pSwapChain, sync, flags);
}

static HRESULT __stdcall HookedResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height,
                                             DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
    __try
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
    }
    __except (resizeGuardFilter(GetExceptionInformation()))
    {
    }

    HRESULT hr = oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

    __try
    {
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
    }
    __except (resizeGuardFilter(GetExceptionInformation()))
    {
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
    mc::resources::unloadAll();
#ifdef _MSC_VER
    D2D::shutdown();
#endif

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
