#include "Render/EffectSprites.hpp"

#include "Framework/Log.hpp"
#include "Framework/stb_image.h"
#include "Render/DX11.hpp"
#include "resource.h"

#include <d3d11.h>
#include <windows.h>

namespace mc::fxsprites {

namespace {

struct Sprite
{
    void* srv = nullptr;
    int w = 0;
    int h = 0;
    bool tried = false;
};

Sprite g_sprites[30];

// carga sin SEH (la usa el wrapper): solo tipos POD, sin unwinding.
bool loadOne(int id)
{
    Sprite& s = g_sprites[id - 1];
    if (s.tried)
        return s.srv != nullptr;
    s.tried = true;

    ID3D11Device* dev = (ID3D11Device*)dx11::d3dDevice();
    if (!dev)
    {
        // Sin device todavia: reintentar en la proxima llamada.
        s.tried = false;
        return false;
    }

    HMODULE mod = (HMODULE)dx11::moduleHandle();
    if (!mod)
        return false;

    HRSRC res = FindResourceW(mod, MAKEINTRESOURCEW(IDR_EFFECT_1 + id - 1), RT_RCDATA);
    if (!res)
    {
        MC_LOG_ERROR("[PotionHUD] sprite %d: resource not found", id);
        return false;
    }
    HGLOBAL hg = LoadResource(mod, res);
    if (!hg)
        return false;
    DWORD size = SizeofResource(mod, res);
    const void* data = LockResource(hg);
    if (!data || !size)
        return false;

    int w = 0, h = 0, n = 0;
    stbi_uc* px = stbi_load_from_memory((const stbi_uc*)data, (int)size, &w, &h, &n, 4);
    if (!px)
    {
        MC_LOG_ERROR("[PotionHUD] sprite %d: decode failed", id);
        return false;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = (UINT)w;
    desc.Height = (UINT)h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = px;
    init.SysMemPitch = (UINT)(w * 4);

    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* view = nullptr;
    if (SUCCEEDED(dev->CreateTexture2D(&desc, &init, &tex)) && tex)
    {
        if (SUCCEEDED(dev->CreateShaderResourceView(tex, nullptr, &view)) && view)
        {
            s.srv = view;
            s.w = w;
            s.h = h;
            MC_LOG("[PotionHUD] sprite %d cargado (%dx%d)", id, w, h);
        }
        tex->Release();
    }

    stbi_image_free(px);
    return s.srv != nullptr;
}

} // namespace

void* srv(int effectId)
{
    if (effectId < 1 || effectId > 30)
        return nullptr;

    if (g_sprites[effectId - 1].srv)
        return g_sprites[effectId - 1].srv;

    __try
    {
        loadOne(effectId);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_sprites[effectId - 1].tried = true;
    }
    return g_sprites[effectId - 1].srv;
}

bool dims(int effectId, int& w, int& h)
{
    if (effectId < 1 || effectId > 30)
        return false;
    if (!srv(effectId))
        return false;
    w = g_sprites[effectId - 1].w;
    h = g_sprites[effectId - 1].h;
    return true;
}

void unloadAll()
{
    for (auto& s : g_sprites)
    {
        if (s.srv)
        {
            ((ID3D11ShaderResourceView*)s.srv)->Release();
            s.srv = nullptr;
        }
        s.tried = false;
        s.w = 0;
        s.h = 0;
    }
}

}
