#include "BlurRTWatch.hpp"

#include "Config/Config.hpp"
#include "Framework/Hook.hpp"

namespace mc::blur::rtwatch {

using namespace detail;

namespace {

using OMSetRenderTargetsFn = void(__stdcall*)(ID3D11DeviceContext*, UINT,
                                               ID3D11RenderTargetView* const*,
                                               ID3D11DepthStencilView*);
OMSetRenderTargetsFn oOMSetRenderTargets = nullptr;
bool g_hookInstalled = false;

ID3D11Resource* volatile g_rtRing[16] = {};
volatile LONG g_rtRingPos = 0;
volatile LONG g_rtRecording = 0;

ID3D11RenderTargetView* g_gameBbRTV = nullptr;
volatile LONG g_proxyOn = 0;
volatile LONG g_proxyUsed = 0;
bool g_proxyEnabled = false;
bool g_proxyEnabledRead = false;

bool proxyConfigEnabled()
{
    if (!g_proxyEnabledRead)
    {
        g_proxyEnabledRead = true;
        g_proxyEnabled = (config::globalSetting("blurProxy", "off") == "on");
        MC_LOG("[Backdrop] Render proxy %s (settings.json: blurProxy)",
               g_proxyEnabled ? "ENABLED" : "disabled");
    }
    return g_proxyEnabled;
}

static void __stdcall hkOMSetRenderTargets(ID3D11DeviceContext* self, UINT NumViews,
                                           ID3D11RenderTargetView* const* ppRTVs,
                                           ID3D11DepthStencilView* pDSV)
{
    if (NumViews > 0 && NumViews <= 8 && ppRTVs && ppRTVs[0])
    {
        if (g_rtRecording)
        {
            __try
            {
                ID3D11Resource* res = nullptr;
                ppRTVs[0]->GetResource(&res);
                if (res)
                {
                    LONG slot = (InterlockedIncrement(&g_rtRingPos) - 1) & 15;
                    ID3D11Resource* prev =
                        (ID3D11Resource*)InterlockedExchangePointer(
                            (PVOID volatile*)&g_rtRing[slot], res);
                    if (prev)
                        prev->Release();
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        // Identificacion perezosa: si aun no conocemos el RTV del backbuffer,
        // el primer RT full-screen visto por el hook lo es.
        if (!g_gameBbRTV && ppRTVs[0] != g_btProxy.rtv && g_displayW > 0.0f)
        {
            __try
            {
                ID3D11Resource* res = nullptr;
                ppRTVs[0]->GetResource(&res);
                if (res)
                {
                    ID3D11Texture2D* t = nullptr;
                    if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)))
                    {
                        D3D11_TEXTURE2D_DESC td = {};
                        t->GetDesc(&td);
                        if (td.Width == (UINT)g_displayW && td.Height == (UINT)g_displayH &&
                            td.SampleDesc.Count == 1)
                        {
                            g_gameBbRTV = ppRTVs[0];
                            MC_LOG("[Backdrop] Game backbuffer RTV (detour): 0x%p",
                                   (void*)g_gameBbRTV);
                        }
                        t->Release();
                    }
                    res->Release();
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        static bool s_logSub = false;
        if (proxyConfigEnabled() && g_proxyOn && g_gameBbRTV && ppRTVs[0] == g_gameBbRTV &&
            g_btProxy.rtv)
        {
            ID3D11RenderTargetView* local[8];
            for (UINT i = 0; i < NumViews; ++i)
                local[i] = ppRTVs[i];
            local[0] = g_btProxy.rtv;
            __try
            {
                oOMSetRenderTargets(self, NumViews, local, pDSV);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return;
            }
            if (!s_logSub)
            {
                s_logSub = true;
                MC_LOG("[Backdrop] Proxy substituted (first bind)");
            }
            InterlockedExchange(&g_proxyUsed, 1);
            return;
        }
    }
    oOMSetRenderTargets(self, NumViews, ppRTVs, pDSV);
}

} // namespace

bool install()
{
    if (g_hookInstalled)
        return true;
    if (!g_context)
        return false;

    // OMSetRenderTargets = slot 33 del vtable ID3D11DeviceContext
    // (verificado contra d3d11.h del SDK).
    void** vt = *(void***)g_context;
    if (!vt)
        return false;
    void* target = vt[33];
    if (!target)
        return false;

    if (!hook::create(target, reinterpret_cast<void*>(&hkOMSetRenderTargets),
                      reinterpret_cast<void**>(&oOMSetRenderTargets)))
    {
        MC_LOG_ERROR("[Backdrop] OMSetRenderTargets hook failed");
        return false;
    }
    g_hookInstalled = true;
    MC_LOG("[Backdrop] OMSetRenderTargets hook installed (slot 33)");
    return true;
}

void setRecording(bool on)
{
    g_rtRecording = on ? 1 : 0;
}

void clearRing()
{
    g_rtRecording = 0;
    for (int i = 0; i < 16; ++i)
    {
        ID3D11Resource* r =
            (ID3D11Resource*)InterlockedExchangePointer((PVOID volatile*)&g_rtRing[i], nullptr);
        if (r)
            r->Release();
    }
}

void armProxy(bool on)
{
    g_proxyOn = (on && proxyConfigEnabled()) ? 1 : 0;
}

bool enabled()
{
    return proxyConfigEnabled();
}

ID3D11RenderTargetView* proxyRtv()
{
    return g_btProxy.rtv;
}

ID3D11ShaderResourceView* proxyFrameSrv()
{
    if (!g_proxyOn || !g_proxyUsed || !g_btProxy.srv)
        return nullptr;
    g_btProxy.srv->AddRef();
    return g_btProxy.srv;
}

void identifyGameBbRTV(ID3D11RenderTargetView* rtv)
{
    // Solo con el proxy desarmado: con el on, el juego bindea NUESTRO proxy.
    if (rtv && rtv != g_btProxy.rtv && g_gameBbRTV != rtv)
    {
        g_gameBbRTV = rtv;
        MC_LOG("[Backdrop] Game backbuffer RTV: 0x%p", (void*)g_gameBbRTV);
    }
}

bool ringCapture(ID3D11ShaderResourceView** outSrv)
{
    *outSrv = nullptr;
    if (!g_hookInstalled)
        return false;

    static bool s_logRing = false;
    LONG newest = g_rtRingPos;
    for (int i = 0; i < 16; ++i)
    {
        LONG slot = (newest - 1 - i) & 15;
        ID3D11Resource* res =
            (ID3D11Resource*)InterlockedExchangePointer((PVOID volatile*)&g_rtRing[slot], nullptr);
        if (!res)
            continue;

        UINT tw = 0, th = 0;
        bool candidate = false;
        ID3D11Texture2D* t = nullptr;
        if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)))
        {
            D3D11_TEXTURE2D_DESC td = {};
            t->GetDesc(&td);
            tw = td.Width;
            th = td.Height;
            candidate = td.Width >= g_displayW * 0.4f &&
                        td.Height >= g_displayH * 0.4f &&
                        td.SampleDesc.Count == 1 && td.MipLevels == 1;
            t->Release();
        }

        if (candidate)
        {
            ID3D11ShaderResourceView* srv = nullptr;
            if (SUCCEEDED(g_device->CreateShaderResourceView(res, nullptr, &srv)) && srv)
            {
                BD_LOG_ONCE(s_logRing, "[Backdrop] Capture via RT ring (%ux%u)", tw, th);
                *outSrv = srv;
            }
        }

        // Devolver la entrada al ring (si el escritor puso otra, gana la suya).
        ID3D11Resource* prev =
            (ID3D11Resource*)InterlockedExchangePointer((PVOID volatile*)&g_rtRing[slot], res);
        if (prev)
            prev->Release();

        if (*outSrv)
            return true;
    }
    return false;
}

void release()
{
    clearRing();
    g_gameBbRTV = nullptr;
    g_proxyOn = 0;
    g_proxyUsed = 0;
    g_proxyEnabledRead = false;
}

} // namespace mc::blur::rtwatch
