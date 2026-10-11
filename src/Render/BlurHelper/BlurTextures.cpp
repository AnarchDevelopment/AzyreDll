#include "BlurTextures.hpp"

#include <algorithm>

namespace mc::blur::detail {

BlurTex g_btHalf;
BlurTex g_btQuarter;
BlurTex g_btSmall;
BlurTex g_btTiny;
BlurTex g_btGaussA;
BlurTex g_btGaussB;
BlurTex g_btProxy;
BlurTex g_ghostRing[detail::kGhostMax];
int g_ghostCursor = 0;
int g_ghostFilled = 0;

} // namespace mc::blur::detail

namespace mc::blur::textures {

using namespace detail;

DXGI_FORMAT makeSrvFormat(DXGI_FORMAT f)
{
    switch (f)
    {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R8_TYPELESS: return DXGI_FORMAT_R8_UNORM;
    case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_FLOAT;
    case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
    default: return f;
    }
}

static void releaseBlurTex(BlurTex& t)
{
    if (t.srv) { t.srv->Release(); t.srv = nullptr; }
    if (t.rtv) { t.rtv->Release(); t.rtv = nullptr; }
    if (t.tex) { t.tex->Release(); t.tex = nullptr; }
    t.w = t.h = 0;
}

static bool createBlurTex(UINT w, UINT h, DXGI_FORMAT format, ID3D11Texture2D** tex,
                         ID3D11RenderTargetView** rtv, ID3D11ShaderResourceView** srv)
{
    static bool s_logTex = false;
    static bool s_logRtv = false;
    static bool s_logSrv = false;

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

    HRESULT hr = g_device->CreateTexture2D(&desc, nullptr, tex);
    if (FAILED(hr))
    {
        BD_LOG_ONCE(s_logTex, "[Backdrop] CreateTexture2D(%ux%u, fmt=%d) failed: 0x%08X",
                    w, h, (int)format, hr);
        return false;
    }
    hr = g_device->CreateRenderTargetView(*tex, nullptr, rtv);
    if (FAILED(hr))
    {
        BD_LOG_ONCE(s_logRtv, "[Backdrop] CreateRenderTargetView failed: 0x%08X", hr);
        (*tex)->Release();
        *tex = nullptr;
        return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
    srvd.Format = makeSrvFormat(format);
    srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvd.Texture2D.MipLevels = 1;
    srvd.Texture2D.MostDetailedMip = 0;
    hr = g_device->CreateShaderResourceView(*tex, &srvd, srv);
    if (FAILED(hr))
    {
        BD_LOG_ONCE(s_logSrv, "[Backdrop] CreateShaderResourceView failed: 0x%08X", hr);
        (*rtv)->Release();
        *rtv = nullptr;
        (*tex)->Release();
        *tex = nullptr;
        return false;
    }
    return true;
}

void release()
{
    releaseBlurTex(g_btHalf);
    releaseBlurTex(g_btQuarter);
    releaseBlurTex(g_btSmall);
    releaseBlurTex(g_btTiny);
    releaseBlurTex(g_btGaussA);
    releaseBlurTex(g_btGaussB);
    releaseBlurTex(g_btProxy);
    for (int i = 0; i < detail::kGhostMax; ++i)
        releaseBlurTex(g_ghostRing[i]);
    g_ghostCursor = 0;
    g_ghostFilled = 0;
}

bool ensure()
{
    if (!g_device || !g_swapChain || g_displayW <= 0.0f || g_displayH <= 0.0f)
        return false;

    UINT wHalf = (UINT)std::max(1.0f, g_displayW * 0.5f);
    UINT hHalf = (UINT)std::max(1.0f, g_displayH * 0.5f);
    UINT wQuarter = (UINT)std::max(1.0f, g_displayW * 0.25f);
    UINT hQuarter = (UINT)std::max(1.0f, g_displayH * 0.25f);
    UINT wSmall = (UINT)std::max(1.0f, g_displayW * 0.125f);
    UINT hSmall = (UINT)std::max(1.0f, g_displayH * 0.125f);
    UINT wTiny = (UINT)std::max(1.0f, g_displayW * 0.0625f);
    UINT hTiny = (UINT)std::max(1.0f, g_displayH * 0.0625f);

    if (g_btHalf.tex && g_btHalf.w == wHalf && g_btHalf.h == hHalf &&
        g_btQuarter.tex && g_btQuarter.w == wQuarter && g_btQuarter.h == hQuarter &&
        g_btSmall.tex && g_btSmall.w == wSmall && g_btSmall.h == hSmall &&
        g_btTiny.tex && g_btTiny.w == wTiny && g_btTiny.h == hTiny &&
        g_btGaussA.tex && g_btGaussB.tex &&
        g_btProxy.tex && g_btProxy.w == (UINT)g_displayW && g_btProxy.h == (UINT)g_displayH)
        return true;

    release();

    // Formato del blur = formato del backbuffer (evita oscurecimiento si el
    // juego corre HDR con formatos float/10bit).
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    {
        ID3D11Texture2D* bb = nullptr;
        if (SUCCEEDED(g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb)))
        {
            D3D11_TEXTURE2D_DESC bd = {};
            bb->GetDesc(&bd);
            fmt = bd.Format;
            bb->Release();
        }
    }
    if (fmt == DXGI_FORMAT_UNKNOWN)
        fmt = DXGI_FORMAT_B8G8R8A8_UNORM;

    BlurTex* targets[6] = {&g_btHalf, &g_btQuarter, &g_btSmall, &g_btTiny, &g_btGaussA, &g_btGaussB};
    const UINT ws[6] = {wHalf, wQuarter, wSmall, wTiny, wSmall, wSmall};
    const UINT hs[6] = {hHalf, hQuarter, hSmall, hTiny, hSmall, hSmall};

    for (int i = 0; i < 6; ++i)
    {
        if (!createBlurTex(ws[i], hs[i], fmt, &targets[i]->tex, &targets[i]->rtv,
                           &targets[i]->srv))
        {
            release();
            return false;
        }
        targets[i]->w = ws[i];
        targets[i]->h = hs[i];
    }

    // Render proxy a resolucion completa (RT+SRV legible).
    if (!createBlurTex((UINT)g_displayW, (UINT)g_displayH, fmt,
                       &g_btProxy.tex, &g_btProxy.rtv, &g_btProxy.srv))
    {
        release();
        return false;
    }
    g_btProxy.w = (UINT)g_displayW;
    g_btProxy.h = (UINT)g_displayH;

    // Ghost ring de MotionBlur a 1/4 de pantalla (20 frames ~10MB a 1080p).
    for (int i = 0; i < detail::kGhostMax; ++i)
    {
        if (!createBlurTex(wQuarter, hQuarter, fmt, &g_ghostRing[i].tex,
                           &g_ghostRing[i].rtv, &g_ghostRing[i].srv))
        {
            release();
            return false;
        }
        g_ghostRing[i].w = wQuarter;
        g_ghostRing[i].h = hQuarter;
    }
    g_ghostCursor = 0;
    g_ghostFilled = 0;
    return true;
}

} // namespace mc::blur::textures
