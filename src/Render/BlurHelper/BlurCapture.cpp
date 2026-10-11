#include "BlurCapture.hpp"

#include "BlurRTWatch.hpp"
#include "BlurTextures.hpp"

#include <algorithm>

namespace mc::blur::detail {

ID3D11Texture2D* g_captureTex = nullptr;
ID3D11ShaderResourceView* g_captureSrv = nullptr;
UINT g_captureW = 0;
UINT g_captureH = 0;

} // namespace mc::blur::detail

namespace mc::blur::capture {

using namespace detail;

ID3D11ShaderResourceView* persistentSrv()
{
    return g_captureSrv;
}

void releaseSource(ID3D11ShaderResourceView* s)
{
    if (s && s != g_captureSrv)
        s->Release();
}

void release()
{
    if (g_captureTex)
    {
        g_captureTex->Release();
        g_captureTex = nullptr;
    }
    if (g_captureSrv)
    {
        g_captureSrv->Release();
        g_captureSrv = nullptr;
    }
    g_captureW = g_captureH = 0;
}

ID3D11ShaderResourceView* find(ID3D11RenderTargetView* boundRtv, bool* outFlip)
{
    static bool s_logInfo = false;
    static bool s_logGetBuffer = false;
    static bool s_logDirect = false;
    static bool s_logClone = false;
    static bool s_logBoundRT = false;
    static bool s_logPsScan = false;

    if (outFlip)
        *outFlip = false;

    HRESULT hr = E_FAIL;
    ID3D11ShaderResourceView* src = nullptr;

    // ------------------------------------------------------------------
    // Fuente 0: el render proxy. Con el proxy activo el juego dibujo su
    // frame directamente en NUESTRA textura (RT+SRV) — captura perfecta.
    // ------------------------------------------------------------------
    src = rtwatch::proxyFrameSrv();
    if (src)
    {
        static bool s_logProxy = false;
        BD_LOG_ONCE(s_logProxy, "[Backdrop] Capture via render proxy");
        return src;
    }

    // ------------------------------------------------------------------
    // Fuente 1: SRV directo del backbuffer (solo swapchains legibles) con
    // clon + CopyResource de respaldo: la via PROBADA de la rama legacy
    // (MotionBlur::CopyBackbufferToSRV) — funciona tambien con backbuffers
    // flip-model en este juego. La textura conserva el formato del
    // backbuffer (CopyResource lo exige) y el SRV usa formato tipado.
    // ------------------------------------------------------------------
    ID3D11Texture2D* bb = nullptr;
    hr = g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
    if (FAILED(hr))
    {
        BD_LOG_ONCE(s_logGetBuffer, "[Backdrop] GetBuffer failed: 0x%08X", hr);
    }
    else
    {
        D3D11_TEXTURE2D_DESC bd = {};
        bb->GetDesc(&bd);
        bool msaa = bd.SampleDesc.Count > 1;

        bool flip = false;
        IDXGISwapChain1* sc1 = nullptr;
        if (SUCCEEDED(g_swapChain->QueryInterface(__uuidof(IDXGISwapChain1), (void**)&sc1)))
        {
            DXGI_SWAP_CHAIN_DESC1 d1 = {};
            sc1->GetDesc1(&d1);
            flip = d1.SwapEffect >= DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            sc1->Release();
        }
        if (outFlip)
            *outFlip = flip;

        if (!s_logInfo)
        {
            s_logInfo = true;
            MC_LOG("[Backdrop] Backbuffer %ux%u fmt=%d msaa=%d flip=%d", bd.Width, bd.Height,
                   (int)bd.Format, msaa ? 1 : 0, flip ? 1 : 0);
        }

        hr = g_device->CreateShaderResourceView(bb, nullptr, &src);
        if (FAILED(hr) || !src)
        {
            src = nullptr;
            BD_LOG_ONCE(s_logDirect, "[Backdrop] Direct SRV failed: 0x%08X", hr);

            bd.SampleDesc.Count = 1;
            bd.SampleDesc.Quality = 0;
            bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            bd.MiscFlags = 0;
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.CPUAccessFlags = 0;

            bool cloneOk = true;
            if (!g_captureTex || g_captureW != bd.Width || g_captureH != bd.Height)
            {
                if (g_captureTex)
                {
                    g_captureTex->Release();
                    g_captureTex = nullptr;
                }
                if (g_captureSrv)
                {
                    g_captureSrv->Release();
                    g_captureSrv = nullptr;
                }
                hr = g_device->CreateTexture2D(&bd, nullptr, &g_captureTex);
                if (FAILED(hr) || !g_captureTex)
                {
                    BD_LOG_ONCE(s_logClone, "[Backdrop] Clone create failed: 0x%08X", hr);
                    cloneOk = false;
                }
                else
                {
                    g_captureW = bd.Width;
                    g_captureH = bd.Height;
                }
            }

            if (cloneOk && !g_captureSrv)
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
                srvd.Format = textures::makeSrvFormat(bd.Format);
                srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvd.Texture2D.MipLevels = 1;
                srvd.Texture2D.MostDetailedMip = 0;
                hr = g_device->CreateShaderResourceView(g_captureTex, &srvd, &g_captureSrv);
                if (FAILED(hr) || !g_captureSrv)
                {
                    BD_LOG_ONCE(s_logClone, "[Backdrop] Clone SRV failed: 0x%08X", hr);
                    cloneOk = false;
                }
            }

            if (cloneOk)
            {
                if (msaa)
                    g_context->ResolveSubresource(g_captureTex, 0, bb, 0, bd.Format);
                else
                    g_context->CopyResource(g_captureTex, bb);
                src = g_captureSrv;
                BD_LOG_ONCE(s_logClone, "[Backdrop] Clone capture ok (%ux%u fmt=%d)",
                            bd.Width, bd.Height, (int)bd.Format);
            }
        }
        bb->Release();
    }

    // ------------------------------------------------------------------
    // Fuente 2 (la clave en flip-model): el ring de render targets que el
    // juego bindeo durante el frame. La escena vive en intermedios
    // SRV-eables (post-proceso).
    // ------------------------------------------------------------------
    if (!src)
    {
        rtwatch::ringCapture(&src);
    }

    // ------------------------------------------------------------------
    // Fuente 3: el render target que el juego dejo bindeado al Present (si
    // presenta desde un intermedio, esto ES la escena final).
    // ------------------------------------------------------------------
    if (!src && boundRtv)
    {
        ID3D11Resource* res = nullptr;
        boundRtv->GetResource(&res);
        if (res)
        {
            hr = g_device->CreateShaderResourceView(res, nullptr, &src);
            if (FAILED(hr) || !src)
            {
                src = nullptr;
            }
            else
            {
                BD_LOG_ONCE(s_logBoundRT, "[Backdrop] Capture via Present-bound RT");
            }
            res->Release();
        }
    }

    // ------------------------------------------------------------------
    // Fuente 4: los SRV que quedaron bindeados en el pixel shader —
    // estado residual del composite final del juego.
    // ------------------------------------------------------------------
    if (!src)
    {
        ID3D11ShaderResourceView* srvs[16] = {};
        g_context->PSGetShaderResources(0, 16, srvs);

        int found = 0;
        UINT bestW = 0, bestH = 0;
        int bestIdx = -1;
        for (int i = 0; i < 16; ++i)
        {
            if (!srvs[i])
                continue;
            ++found;
            ID3D11Resource* r = nullptr;
            srvs[i]->GetResource(&r);
            if (r)
            {
                ID3D11Texture2D* t = nullptr;
                if (SUCCEEDED(r->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&t)))
                {
                    D3D11_TEXTURE2D_DESC td = {};
                    t->GetDesc(&td);
                    if (td.Width >= g_displayW * 0.4f && td.Height >= g_displayH * 0.4f &&
                        td.SampleDesc.Count == 1 &&
                        (float)td.Width > (float)std::max(1u, td.Height) * 1.2f &&
                        td.Width > bestW)
                    {
                        bestW = td.Width;
                        bestH = td.Height;
                        bestIdx = i;
                    }
                    t->Release();
                }
                r->Release();
            }
        }

        if (bestIdx >= 0)
        {
            src = srvs[bestIdx];
            src->AddRef();
            BD_LOG_ONCE(s_logPsScan, "[Backdrop] Capture via leftover PS SRV (%ux%u)", bestW, bestH);
        }
        else
        {
            BD_LOG_ONCE(s_logPsScan, "[Backdrop] PS scan: %d srvs, none screen-sized", found);
        }

        for (int i = 0; i < 16; ++i)
        {
            if (srvs[i])
                srvs[i]->Release();
        }
    }

    return src;
}

} // namespace mc::blur::capture
