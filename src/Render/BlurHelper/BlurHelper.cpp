#include "BlurHelper.hpp"

#include "BlurCapture.hpp"
#include "BlurDetail.hpp"
#include "BlurPasses.hpp"
#include "BlurRTWatch.hpp"
#include "BlurShaders.hpp"
#include "BlurTextures.hpp"

#include "GUI/Menu.hpp"
#include "Modules/ModuleManager.hpp"

namespace mc::blur {

namespace detail {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
float g_displayW = 0.0f;
float g_displayH = 0.0f;

} // namespace detail

namespace {

bool g_broken = false;       // SEH desactivo el pase para siempre
bool g_hasContent = false;   // gaussB tiene un blur valido del frame actual
bool g_debugCompositeRaw = false; // F11: componer la captura RAW sin blur
bool g_mbEnabled = false;          // MotionBlur (arst-motion-blur) activo
float g_mbStrength = 0.75f;        // Factor de blur (0..0.99)
bool g_menuGlass = true;          // composite Glass del menu (off en modo Solstice)

// Paneles acrilicos de los widgets registrados este frame (hilo de Present,
// mismo hilo que runPass: sin mutex). Se limpian en beginAcrylic().
struct AcrylicRect
{
    float x1, y1, x2, y2;
    float radius;
    float alpha;
};
std::vector<AcrylicRect> g_acrylic;

void passBody(ID3D11RenderTargetView* boundRtv, bool allowWrites)
{
    if (!detail::g_context || !detail::g_swapChain)
        return;
    if (!textures::ensure())
        return;

    static ULONGLONG s_logNoSrc = 0;
    static bool s_logPassOk = false;

    bool flip = false;
    ID3D11ShaderResourceView* src = capture::find(boundRtv, &flip, allowWrites);
    if (!src)
    {
        g_hasContent = false;
        BD_LOG_EVERY(s_logNoSrc, 2000,
                     "[Backdrop] No readable capture source (flip=%d) - dim fallback",
                     flip ? 1 : 0);
        return;
    }

    detail::g_context->OMSetRenderTargets(0, nullptr, nullptr);

    shaders::ensure();

    passes::StateBag bag;
    passes::save(bag);

    // ------------------------------------------------------------------
    // MotionBlur (arst-motion-blur): procesa el frame del juego a resolución
    // completa ANTES de que se dibuje cualquier widget o HUD de ImGui.
    // Solo afecta al juego: cuando el menú está abierto se pausa y resetea.
    // ------------------------------------------------------------------
    bool menuOpen = mc::menu::visible() || mc::menu::animationAlpha() > 0.001f;
    if (g_mbEnabled && !menuOpen && detail::g_mbPrev.rtv && detail::g_mbSwap.rtv)
    {
        UINT dispW = (UINT)detail::g_displayW;
        UINT dispH = (UINT)detail::g_displayH;
        float factor = std::clamp(g_mbStrength, 0.0f, 0.99f);

        if (!detail::g_mbHasHistory)
        {
            if (detail::g_captureTex)
                detail::g_context->CopyResource(detail::g_mbPrev.tex, detail::g_captureTex);
            else
                passes::copy(src, detail::g_mbPrev.rtv, dispW, dispH);
            detail::g_mbHasHistory = true;
        }
        else
        {
            // Pass 1: blend del frame actual (src) y el previo acumulado (g_mbPrev) hacia swap
            passes::motionBlur(src, detail::g_mbPrev.srv, detail::g_mbSwap.rtv, dispW, dispH, factor);

            // Pass 2: actualizar el buffer acumulado previo con el resultado actual
            detail::g_context->CopyResource(detail::g_mbPrev.tex, detail::g_mbSwap.tex);

            // Pass 3: volcar el frame con MotionBlur al backbuffer real del
            // juego — suspendido con pantallas del juego abiertas.
            if (allowWrites)
            {
                ID3D11Texture2D* bb = nullptr;
                if (SUCCEEDED(detail::g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)))
                {
                    ID3D11RenderTargetView* bbRtv = nullptr;
                    if (SUCCEEDED(detail::g_device->CreateRenderTargetView(bb, nullptr, &bbRtv)) && bbRtv)
                    {
                        passes::copy(detail::g_mbSwap.srv, bbRtv, dispW, dispH);
                        bbRtv->Release();
                    }
                    bb->Release();
                }
            }
        }
    }
    else if (menuOpen)
    {
        detail::g_mbHasHistory = false;
    }

    // Con el menu cerrado y sin modulos que quieran el backdrop, el ghost es
    // lo unico necesario: se salta la cadena (downscale + gauss + composite).
    float menuAnim = mc::menu::animationAlpha();
    bool needChain = menuAnim > 0.001f;
    if (!needChain)
    {
        auto modOn = [](const char* name) {
            mc::Module* m = mc::ModuleManager::get().find(name);
            return m && m->enabled();
        };
        needChain = modOn("HUD") || modOn("ArrayList") || modOn("Watermark") ||
                    modOn("PotionHUD") || modOn("NameTags");
    }
    // Widgets acrilicos registrados este frame (PlayerList, etc): necesitan la
    // cadena de blur aunque no haya modulos "de blur" ni el menu abierto.
    if (!needChain && !g_acrylic.empty())
        needChain = true;
    if (!needChain)
    {
        passes::restore(bag);
        detail::g_context->OMSetRenderTargets(0, nullptr, nullptr);
        capture::releaseSource(src);
        g_hasContent = false;
        return;
    }

    // Cadena progresiva de halving: a escala 2 la interpolacion bilinear cae
    // exactamente entre texels, produciendo un box 2x2 real por paso.
    passes::copy(src, detail::g_btHalf.rtv, detail::g_btHalf.w, detail::g_btHalf.h);
    passes::copy(detail::g_btHalf.srv, detail::g_btQuarter.rtv, detail::g_btQuarter.w,
                 detail::g_btQuarter.h);
    passes::copy(detail::g_btQuarter.srv, detail::g_btSmall.rtv, detail::g_btSmall.w,
                 detail::g_btSmall.h);

    // Gaussiano separable a 1/8, DOS iteraciones H/V: sigma ~19px a
    // resolucion completa — el frost de una sola iteracion (~14px) se veia
    // demasiado debil detras de los widgets. El resultado sigue en GaussB
    // (lo consumen srv() y los composites).
    if (shaders::ok())
    {
        passes::gauss(detail::g_btSmall.srv, detail::g_btGaussA.rtv, detail::g_btGaussA.w,
                      detail::g_btGaussA.h, 1.0f, 0.0f);
        passes::gauss(detail::g_btGaussA.srv, detail::g_btGaussB.rtv, detail::g_btGaussB.w,
                      detail::g_btGaussB.h, 0.0f, 1.0f);
        passes::gauss(detail::g_btGaussB.srv, detail::g_btGaussA.rtv, detail::g_btGaussA.w,
                      detail::g_btGaussA.h, 1.0f, 0.0f);
        passes::gauss(detail::g_btGaussA.srv, detail::g_btGaussB.rtv, detail::g_btGaussB.w,
                      detail::g_btGaussB.h, 0.0f, 1.0f);

        // Composite Mica + Glass directo al backbuffer (puerto del legacy
        // ClickGUI::mainCompositePS): la escena escarchada REEMPLAZA la
        // pantalla — sin overlays ImGui, sin blending. RTV fresco del
        // backbuffer (el del juego puede ser null en Present).
        bool compMenu = (menuAnim > 0.001f) && g_menuGlass;
        bool compWidgets = !g_acrylic.empty();
        // Con una pantalla del juego abierta no se escribe NADA al
        // backbuffer: los composites Glass/Mica quedan suspendidos. Los
        // widgets acrilicos siguen mostrando blur en vivo porque dibujan
        // srv() en su propia draw list (la cadena sigue corriendo).
        if (allowWrites && (compMenu || compWidgets))
        {
            // F11: componer la RAW (contenido conocido-bueno) aisla el
            // composite de la cadena de blur.
            ID3D11ShaderResourceView* compSrc = g_debugCompositeRaw
                                                     ? capture::persistentSrv()
                                                     : detail::g_btGaussB.srv;
            static bool s_logComp = false;
            if (!s_logComp)
            {
                s_logComp = true;
                MC_LOG("[Backdrop] Mica/Glass composite active (raw=%d, anim=%.2f, rects=%d)",
                       g_debugCompositeRaw ? 1 : 0, menuAnim, (int)g_acrylic.size());
            }
            if (compSrc)
            {
                ID3D11Texture2D* bb = nullptr;
                if (SUCCEEDED(detail::g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                                              (void**)&bb)))
                {
                    ID3D11RenderTargetView* bbRtv = nullptr;
                    if (SUCCEEDED(detail::g_device->CreateRenderTargetView(bb, nullptr, &bbRtv)) &&
                        bbRtv)
                    {
                        if (compMenu)
                        {
                            //  1. BACKGROUND: blur CLARO a pantalla completa —
                            //     apenas un susurro de Mica, la escena nitida
                            //     manda.
                            passes::composite(compSrc, bbRtv,
                                              (UINT)detail::g_displayW, (UINT)detail::g_displayH,
                                              0.13f, 0.13f, 0.15f, 0.07f * menuAnim);

                            //  2. GUI: Glass Fluent redondeado — el vidrio del
                            //     menu con borde SDF, refraccion y luz de borde.
                            //     Emite alpha dentro del rounded-rect, asi las
                            //     esquinas quedan exactas (sin frost cuadrado).
                            ImVec2 wMin, wMax;
                            mc::menu::windowRect(wMin, wMax);
                            if (wMax.x > wMin.x && wMax.y > wMin.y)
                            {
                                passes::compositeGlass(compSrc, bbRtv,
                                                       (UINT)detail::g_displayW,
                                                       (UINT)detail::g_displayH,
                                                       wMin.x, wMin.y, wMax.x, wMax.y,
                                                       mc::menu::glassRadius(),
                                                       1.0f,     // softness (feather px)
                                                       9.0f,     // refraccion px en el borde
                                                       2.4f,     // aberracion cromatica px
                                                       0.09f, 0.11f, 0.15f, 0.42f,
                                                       0.93f * menuAnim);
                            }
                        }

                        //  3. Widgets acrilicos (HUD): mismo shader Glass, uno
                        //     por panel registrado. Tambien con el menu cerrado
                        //     durante el gameplay — asi un panel sobre un
                        //     objeto muestra frost real y no un glow.
                        for (const AcrylicRect& r : g_acrylic)
                        {
                            passes::compositeGlass(compSrc, bbRtv,
                                                   (UINT)detail::g_displayW,
                                                   (UINT)detail::g_displayH,
                                                   r.x1, r.y1, r.x2, r.y2,
                                                   r.radius,
                                                   1.0f,     // softness
                                                   9.0f,     // refraccion px
                                                   2.4f,     // croma px
                                                   0.10f, 0.12f, 0.16f, 0.42f,
                                                   r.alpha);
                        }

                        bbRtv->Release();
                    }
                    bb->Release();
                }
            }
        }
    }

    passes::restore(bag);

    detail::g_context->OMSetRenderTargets(0, nullptr, nullptr);

    capture::releaseSource(src);

    g_hasContent = true;

    if (!s_logPassOk)
    {
        s_logPassOk = true;
        MC_LOG("[Backdrop] pipeline ok (%s)", shaders::ok() ? "gaussian" : "box chain");
    }
}

} // namespace

void init(ID3D11Device* device, ID3D11DeviceContext* context, IDXGISwapChain* swapChain)
{
    detail::g_device = device;
    detail::g_context = context;
    detail::g_swapChain = swapChain;
}

void setDisplaySize(float w, float h)
{
    detail::g_displayW = w;
    detail::g_displayH = h;
}

void onResize()
{
    textures::release();
    capture::release();
    rtwatch::clearRing();
}

void shutdown()
{
    textures::release();
    shaders::release();
    capture::release();
    rtwatch::release();
    g_hasContent = false;
}

void runPass(ID3D11RenderTargetView* boundRtv, bool allowBackbufferWrites)
{
    if (g_broken)
        return;
    __try
    {
        passBody(boundRtv, allowBackbufferWrites);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_broken = true;
        // Sin remanentes: la SEH desactiva el pase — bajar el flag hace que
        // el menu muestre el dim y no el ultimo blur congelado.
        g_hasContent = false;
        detail::g_context->OMSetRenderTargets(0, nullptr, nullptr);
        MC_LOG_ERROR("[Backdrop] Excepcion durante el pase de blur - blur desactivado");
    }
}

bool wanted()
{
    if (mc::menu::visible())
        return true;

    // MotionBlur: acumulación EMA necesita el pase cada frame.
    if (g_mbEnabled)
        return true;

    // Widgets acrilicos registrados este frame (PlayerList, etc).
    if (!g_acrylic.empty())
        return true;

    // Modulos con panel acrilico: blur en vivo durante el juego (throttled).
    auto modOn = [](const char* name) {
        mc::Module* m = mc::ModuleManager::get().find(name);
        return m && m->enabled();
    };
    if (modOn("HUD") || modOn("ArrayList") || modOn("Watermark") || modOn("PotionHUD"))
        return true;

    // NameTags (usa el blur para el fondo de los tags).
    return modOn("NameTags");
}

bool hasAcrylic()
{
    return !g_acrylic.empty();
}

void setMenuGlass(bool on)
{
    g_menuGlass = on;
}

bool ready()
{
    return textures::ensure();
}

void* srv()
{
    if (!g_hasContent)
        return nullptr;
    if (shaders::ok() && detail::g_btGaussB.srv)
        return (void*)detail::g_btGaussB.srv;
    return (void*)detail::g_btSmall.srv;
}

void* captureSrv()
{
    if (!g_hasContent)
        return nullptr;
    return (void*)capture::persistentSrv();
}

void setMotionBlurEnabled(bool on)
{
    if (g_mbEnabled == on)
        return;
    g_mbEnabled = on;
    if (!on)
        resetMotionBlur();
    MC_LOG("[Backdrop] MotionBlur %s", on ? "ON" : "OFF");
}

bool motionBlurEnabled()
{
    return g_mbEnabled;
}

void setMotionBlurStrength(float strength)
{
    g_mbStrength = strength;
}

float motionBlurStrength()
{
    return g_mbStrength;
}

void resetMotionBlur()
{
    detail::g_mbHasHistory = false;
}

void identifyGameBbRTV(ID3D11RenderTargetView* rtv)
{
    rtwatch::identifyGameBbRTV(rtv);
}

void setRecording(bool on)
{
    rtwatch::setRecording(on);
}

void armProxy(bool on)
{
    rtwatch::armProxy(on);
}

void clearRing()
{
    rtwatch::clearRing();
}

void* takeProxyFrame()
{
    if (g_mbEnabled && !mc::menu::visible())
        return nullptr;
    return (void*)rtwatch::proxyFrameSrv();
}

void setDebugCompositeRaw(bool on)
{
    static bool s_last = false;
    g_debugCompositeRaw = on;
    if (on != s_last)
    {
        s_last = on;
        MC_LOG("[Backdrop] Debug composite raw %s", on ? "ON (F11)" : "off");
    }
}

void beginAcrylic()
{
    g_acrylic.clear();
}

void addAcrylicRect(float x1, float y1, float x2, float y2, float radius, float alpha)
{
    if (alpha <= 0.01f || x2 <= x1 || y2 <= y1)
        return;
    if (g_acrylic.size() >= 24)
        return;
    AcrylicRect r;
    r.x1 = x1;
    r.y1 = y1;
    r.x2 = x2;
    r.y2 = y2;
    r.radius = radius;
    r.alpha = alpha;
    g_acrylic.push_back(r);
}

} // namespace mc::blur
