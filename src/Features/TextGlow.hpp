#pragma once

#include "Features/TextGradient.hpp"
#include "Render/DX11.hpp"

#include <d3d11.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace mc::features {

struct PerCharacterGlowConfig
{
    float radius = 14.0f;       // Visible extent of the halo, in pixels.
    float intensity = 1.0f;
    float falloffPower = 1.0f;  // Retained for API compatibility; Gaussian uses radius.
    int rings = 4;              // Retained for API compatibility.
    int ringSamples = 24;       // Retained for API compatibility.
};

namespace glow_detail {

struct CachedGlow
{
    ImFont* font = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    std::string text;
    float size = 0.0f;
    float radius = 0.0f;
    ImVec2 textureSize{};
    ImVec2 textOffset{};
    ImVec2 textSize{};

    ~CachedGlow()
    {
        if (srv)
            srv->Release();
    }
};

inline CachedGlow& cache()
{
    static CachedGlow value;
    return value;
}

inline unsigned char sourceAlpha(const ImTextureData* texture, int x, int y)
{
    if (!texture || !texture->Pixels || x < 0 || y < 0 || x >= texture->Width || y >= texture->Height)
        return 0;
    const unsigned char* pixel = texture->Pixels +
        (static_cast<size_t>(y) * texture->Width + x) * texture->BytesPerPixel;
    return texture->BytesPerPixel == 1 ? pixel[0] : pixel[3];
}

inline bool build(CachedGlow& out, ImFont* font, ImFontBaked* baked,
                  float size, float radius, const char* text)
{
    if (!font || !font->OwnerAtlas || !font->OwnerAtlas->TexData ||
        !font->OwnerAtlas->TexData->Pixels || !baked || !text)
        return false;

    ID3D11Device* device = static_cast<ID3D11Device*>(dx11::d3dDevice());
    ImTextureData* atlas = font->OwnerAtlas->TexData;
    if (!device || atlas->Width <= 0 || atlas->Height <= 0 ||
        (atlas->BytesPerPixel != 1 && atlas->BytesPerPixel != 4))
        return false;

    struct GlyphPlacement { ImFontGlyph* glyph; float x; float y; };
    std::vector<GlyphPlacement> glyphs;
    const char* end = text + std::strlen(text);
    const float scale = size / baked->Size;
    float cursorX = 0.0f;
    float cursorY = 0.0f;
    float maxWidth = 0.0f;
    int lines = 1;
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = size;

    for (const char* p = text; p < end;)
    {
        unsigned int cp = 0;
        const char* next = detail::nextUtf8(p, end, cp);
        if (cp == '\n')
        {
            maxWidth = std::max(maxWidth, cursorX);
            cursorX = 0.0f;
            cursorY += size;
            ++lines;
            p = next;
            continue;
        }
        if (cp == '\r')
        {
            p = next;
            continue;
        }

        ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(cp));
        if (glyph)
        {
            if (glyph->Visible)
            {
                glyphs.push_back({glyph, cursorX, cursorY});
                minX = std::min(minX, cursorX + glyph->X0 * scale);
                minY = std::min(minY, cursorY + glyph->Y0 * scale);
                maxX = std::max(maxX, cursorX + glyph->X1 * scale);
                maxY = std::max(maxY, cursorY + glyph->Y1 * scale);
            }
            cursorX += glyph->AdvanceX * scale;
        }
        p = next;
    }
    maxWidth = std::max(maxWidth, cursorX);
    if (glyphs.empty() || maxX <= minX || maxY <= minY)
        return false;

    const float sigma = std::max(1.0f, radius * 0.42f);
    const int padding = std::max(2, static_cast<int>(std::ceil(radius * 1.5f)));
    const int width = static_cast<int>(std::ceil(maxX - minX)) + padding * 2;
    const int height = static_cast<int>(std::ceil(maxY - minY)) + padding * 2;
    if (width <= 0 || height <= 0 || width > 1024 || height > 256)
        return false;

    std::vector<float> mask(static_cast<size_t>(width) * height, 0.0f);
    for (const GlyphPlacement& placement : glyphs)
    {
        const ImFontGlyph& glyph = *placement.glyph;
        const float left = placement.x + glyph.X0 * scale - minX + padding;
        const float top = placement.y + glyph.Y0 * scale - minY + padding;
        const int gw = std::max(1, static_cast<int>(std::ceil((glyph.X1 - glyph.X0) * scale)));
        const int gh = std::max(1, static_cast<int>(std::ceil((glyph.Y1 - glyph.Y0) * scale)));
        for (int gy = 0; gy < gh; ++gy)
        {
            const int dy = static_cast<int>(top) + gy;
            if (dy < 0 || dy >= height)
                continue;
            const float v = glyph.V0 + (glyph.V1 - glyph.V0) *
                ((static_cast<float>(gy) + 0.5f) / static_cast<float>(gh));
            const int sy = static_cast<int>(v * atlas->Height);
            for (int gx = 0; gx < gw; ++gx)
            {
                const int dx = static_cast<int>(left) + gx;
                if (dx < 0 || dx >= width)
                    continue;
                const float u = glyph.U0 + (glyph.U1 - glyph.U0) *
                    ((static_cast<float>(gx) + 0.5f) / static_cast<float>(gw));
                const int sx = static_cast<int>(u * atlas->Width);
                const float alpha = sourceAlpha(atlas, sx, sy) / 255.0f;
                float& target = mask[static_cast<size_t>(dy) * width + dx];
                target = std::max(target, alpha);
            }
        }
    }

    const int kernelRadius = std::max(1, static_cast<int>(std::ceil(sigma * 3.0f)));
    std::vector<float> kernel(static_cast<size_t>(kernelRadius * 2 + 1));
    float kernelSum = 0.0f;
    for (int i = -kernelRadius; i <= kernelRadius; ++i)
    {
        const float weight = std::exp(-0.5f * static_cast<float>(i * i) / (sigma * sigma));
        kernel[static_cast<size_t>(i + kernelRadius)] = weight;
        kernelSum += weight;
    }
    for (float& weight : kernel)
        weight /= kernelSum;

    std::vector<float> temp(mask.size(), 0.0f);
    std::vector<float> blurred(mask.size(), 0.0f);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            float sum = 0.0f;
            for (int k = -kernelRadius; k <= kernelRadius; ++k)
            {
                const int sx = std::clamp(x + k, 0, width - 1);
                sum += mask[static_cast<size_t>(y) * width + sx] *
                    kernel[static_cast<size_t>(k + kernelRadius)];
            }
            temp[static_cast<size_t>(y) * width + x] = sum;
        }
    }
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            float sum = 0.0f;
            for (int k = -kernelRadius; k <= kernelRadius; ++k)
            {
                const int sy = std::clamp(y + k, 0, height - 1);
                sum += temp[static_cast<size_t>(sy) * width + x] *
                    kernel[static_cast<size_t>(k + kernelRadius)];
            }
            blurred[static_cast<size_t>(y) * width + x] = sum;
        }
    }

    std::vector<uint32_t> rgba(static_cast<size_t>(width) * height);
    for (size_t i = 0; i < rgba.size(); ++i)
    {
        const unsigned int a = static_cast<unsigned int>(std::clamp(blurred[i] * 255.0f, 0.0f, 255.0f));
        rgba[i] = (a << 24) | 0x00FFFFFFu;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = rgba.data();
    init.SysMemPitch = static_cast<UINT>(width * sizeof(uint32_t));

    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    if (FAILED(device->CreateTexture2D(&desc, &init, &texture)) || !texture)
        return false;
    const HRESULT viewResult = device->CreateShaderResourceView(texture, nullptr, &srv);
    texture->Release();
    if (FAILED(viewResult) || !srv)
        return false;

    out.font = font;
    out.device = device;
    out.srv = srv;
    out.text = text;
    out.size = size;
    out.radius = radius;
    out.textureSize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    out.textOffset = ImVec2(minX - padding, minY - padding);
    out.textSize = ImVec2(maxWidth, std::max(size, static_cast<float>(lines) * size));
    return true;
}

inline CachedGlow* get(ImFont* font, ImFontBaked* baked, float size,
                       float radius, const char* text)
{
    CachedGlow& current = cache();
    ID3D11Device* device = static_cast<ID3D11Device*>(dx11::d3dDevice());
    if (current.srv && current.font == font && current.device == device &&
        current.size == size && current.radius == radius && current.text == text)
        return &current;

    if (current.srv)
    {
        current.srv->Release();
        current.srv = nullptr;
    }
    current.font = nullptr;
    current.device = nullptr;
    current.text.clear();
    if (!build(current, font, baked, size, radius, text))
        return nullptr;
    return &current;
}

} // namespace glow_detail

// Draw a genuinely blurred alpha mask tinted with the active theme. The caller
// should draw its sharp/gradient text afterward to preserve readability.
inline ImVec2 addThemeTextGlow(ImDrawList* drawList, ImFont* font, float size,
                               const ImVec2& position, const char* text,
                               const PerCharacterGlowConfig& config = {})
{
    if (!drawList || !font || !font->OwnerAtlas || !text || !*text || size <= 0.0f)
        return ImVec2(0.0f, 0.0f);

    const float radius = std::clamp(std::isfinite(config.radius) ? config.radius : 14.0f,
                                    2.0f, 24.0f);
    glow_detail::CachedGlow* glow = glow_detail::get(
        font, font->GetFontBaked(size), size, radius, text);
    if (!glow || !glow->srv)
        return ImVec2(0.0f, 0.0f);

    const float strength = std::clamp(
        std::isfinite(config.intensity) ? config.intensity : 1.0f, 0.0f, 2.0f);
    const ImU32 tint = detail::packColor(
        detail::animatedThemeColor(0.5f, ImGui::GetTime()), strength);
    const ImVec2 topLeft(position.x + glow->textOffset.x,
                         position.y + glow->textOffset.y);
    const ImVec2 bottomRight(topLeft.x + glow->textureSize.x,
                             topLeft.y + glow->textureSize.y);
    drawList->AddImage(ImTextureRef(static_cast<void*>(glow->srv)), topLeft,
                       bottomRight, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
    return glow->textSize;
}

} // namespace mc::features
