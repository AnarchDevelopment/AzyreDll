#include "Render/ResourceSprites.hpp"

#include "Framework/Log.hpp"
#include "Framework/stb_image.h"
#include "Render/DX11.hpp"

#include <d3d11.h>
#include <windows.h>

#include <unordered_map>

namespace mc::resources {

namespace {

struct Sprite
{
    void* srv = nullptr;
    int w = 0;
    int h = 0;
    bool tried = false;
};

std::unordered_map<int, Sprite>& cache()
{
    static std::unordered_map<int, Sprite> m;
    return m;
}

// Sin SEH (la usa el wrapper): solo tipos POD, sin unwinding.
bool loadOne(int resourceId)
{
    Sprite& s = cache()[resourceId];
    if (s.tried)
        return s.srv != nullptr;
    s.tried = true;

    ID3D11Device* dev = (ID3D11Device*)dx11::d3dDevice();
    if (!dev)
    {
        s.tried = false; // sin device todavia: reintentar despues
        return false;
    }

    HMODULE mod = (HMODULE)dx11::moduleHandle();
    if (!mod)
        return false;

    HRSRC res = FindResourceW(mod, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!res)
    {
        MC_LOG_ERROR("[Res] %d: resource not found", resourceId);
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
        MC_LOG_ERROR("[Res] %d: decode failed", resourceId);
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
            MC_LOG("[Res] %d cargado (%dx%d)", resourceId, w, h);
        }
        tex->Release();
    }

    stbi_image_free(px);
    return s.srv != nullptr;
}

} // namespace

void* srv(int resourceId)
{
    Sprite& s = cache()[resourceId];
    if (s.srv)
        return s.srv;

    __try
    {
        loadOne(resourceId);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        s.tried = true;
    }
    return s.srv;
}

void unloadAll()
{
    for (auto& kv : cache())
    {
        if (kv.second.srv)
        {
            ((ID3D11ShaderResourceView*)kv.second.srv)->Release();
            kv.second.srv = nullptr;
        }
    }
    cache().clear();
}

}
