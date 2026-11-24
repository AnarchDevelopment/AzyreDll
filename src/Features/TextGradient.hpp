#pragma once

#include "GUI/Theme.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace mc::features {

namespace detail {

inline ImVec4 safeColor(ImVec4 c)
{
    const auto channel = [](float v) {
        return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 1.0f;
    };
    return ImVec4(channel(c.x), channel(c.y), channel(c.z), 1.0f);
}

inline ImVec4 mixColor(const ImVec4& a, const ImVec4& b, float t)
{
    return ImVec4(a.x + (b.x - a.x) * t,
                  a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t, 1.0f);
}

inline float fract(float v)
{
    return v - std::floor(v);
}

// Sample a moving theme palette. Accent palettes travel at 0.8 cycles/sec;
// Rainbow is accelerated separately from the slower default theme loop.
inline ImVec4 animatedThemeColor(float phase, double time)
{
    if (theme::AccentRainbow)
        return safeColor(theme::rainbowAt(phase * 360.0f +
                                          static_cast<float>(time * 300.0), 0.0f));

    phase = fract(phase + static_cast<float>(time * 0.8));

    const ImVec4 stops[3] = {
        safeColor(theme::Accent),
        safeColor(theme::Accent2),
        safeColor(theme::Accent3),
    };
    const int stopCount = theme::AccentTri ? 3 : 2;
    float paletteDelta = 0.0f;
    for (int i = 1; i < stopCount; ++i)
    {
        paletteDelta += std::fabs(stops[0].x - stops[i].x) +
                        std::fabs(stops[0].y - stops[i].y) +
                        std::fabs(stops[0].z - stops[i].z);
    }

    if (paletteDelta < 0.015f)
    {
        // Monochrome themes retain their hue while a visible light wave travels
        // through each glyph.
        const float light = 0.45f + 0.55f *
            (0.5f + 0.5f * std::sin(phase * 6.2831853f));
        return ImVec4(std::clamp(stops[0].x * light, 0.0f, 1.0f),
                      std::clamp(stops[0].y * light, 0.0f, 1.0f),
                      std::clamp(stops[0].z * light, 0.0f, 1.0f), 1.0f);
    }

    const float point = phase * static_cast<float>(stopCount);
    const int segment = static_cast<int>(point) % stopCount;
    return mixColor(stops[segment], stops[(segment + 1) % stopCount],
                    point - std::floor(point));
}

inline const char* nextUtf8(const char* p, const char* end, unsigned int& codepoint)
{
    const unsigned char lead = static_cast<unsigned char>(*p);
    int length = 1;
    if ((lead & 0xE0) == 0xC0) length = 2;
    else if ((lead & 0xF0) == 0xE0) length = 3;
    else if ((lead & 0xF8) == 0xF0) length = 4;

    if (p + length > end)
        length = 1;

    codepoint = lead;
    if (length == 2) codepoint = lead & 0x1F;
    else if (length == 3) codepoint = lead & 0x0F;
    else if (length == 4) codepoint = lead & 0x07;
    for (int i = 1; i < length; ++i)
    {
        const unsigned char continuation = static_cast<unsigned char>(p[i]);
        if ((continuation & 0xC0) != 0x80)
        {
            length = 1;
            codepoint = 0xFFFD;
            break;
        }
        codepoint = (codepoint << 6) | (continuation & 0x3F);
    }
    return p + length;
}

inline ImU32 packColor(ImVec4 c, float alpha)
{
    c = safeColor(c);
    c.w = std::clamp(std::isfinite(alpha) ? alpha : 1.0f, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(c);
}

} // namespace detail

// Draw a bilinear, animated theme gradient inside each UTF-8 glyph quad.
// Returns the text bounds so callers can position adjacent text.
inline ImVec2 addThemeGradientText(ImDrawList* drawList, ImFont* font,
                                   float size, const ImVec2& position,
                                   const char* text, float alpha = 1.0f,
                                   float directionDegrees = 0.0f)
{
    if (!drawList || !font || !font->OwnerAtlas || !text || !*text || size <= 0.0f)
        return ImVec2(0.0f, 0.0f);

    const char* textEnd = text + std::strlen(text);
    ImFontBaked* baked = font->GetFontBaked(size);
    if (!baked || baked->Size <= 0.0f)
        return ImVec2(0.0f, 0.0f);

    const float scale = size / baked->Size;
    float maxLineWidth = 0.0f;
    float measuringLineWidth = 0.0f;
    int lineCount = 1;
    for (const char* p = text; p < textEnd;)
    {
        unsigned int codepoint = 0;
        p = detail::nextUtf8(p, textEnd, codepoint);
        if (codepoint == '\n')
        {
            maxLineWidth = std::max(maxLineWidth, measuringLineWidth);
            measuringLineWidth = 0.0f;
            ++lineCount;
            continue;
        }
        if (ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(codepoint)))
            measuringLineWidth += glyph->AdvanceX * scale;
    }
    maxLineWidth = std::max(maxLineWidth, measuringLineWidth);
    const float textHeight = std::max(size, static_cast<float>(lineCount) * size);

    float x = std::trunc(position.x);
    const float originY = std::trunc(position.y);
    const float originX = x;
    float y = originY;
    const double time = ImGui::GetTime();
    constexpr float kPi = 3.14159265358979323846f;
    const float safeDirection = std::isfinite(directionDegrees)
        ? std::clamp(directionDegrees, 0.0f, 360.0f)
        : 0.0f;
    const float angle = safeDirection * (kPi / 180.0f);
    const float dirX = std::cos(angle);
    const float dirY = std::sin(angle);
    const float xProjection = maxLineWidth * dirX;
    const float yProjection = textHeight * dirY;
    const float projectionMin = std::min(0.0f, xProjection) + std::min(0.0f, yProjection);
    const float projectionMax = std::max(0.0f, xProjection) + std::max(0.0f, yProjection);
    const float projectionRange = std::max(0.001f, projectionMax - projectionMin);
    // Keep roughly one palette pass per five glyph widths. This scales with
    // the measured run, so longer words extend the gradient naturally without
    // flattening the four-corner gradient inside each character.
    const float cycles = std::max(1.0f, projectionRange / std::max(1.0f, size * 0.72f * 5.0f));
    const float lineAdvance = size;

    drawList->PushTexture(font->OwnerAtlas->TexRef);
    for (const char* p = text; p < textEnd;)
    {
        unsigned int codepoint = 0;
        const char* next = detail::nextUtf8(p, textEnd, codepoint);
        if (codepoint == '\n')
        {
            x = originX;
            y += lineAdvance;
            p = next;
            continue;
        }

        ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(codepoint));
        if (!glyph)
        {
            p = next;
            continue;
        }

        if (glyph->Visible)
        {
            const float x0 = x + glyph->X0 * scale;
            const float y0 = y + glyph->Y0 * scale;
            const float x1 = x + glyph->X1 * scale;
            const float y1 = y + glyph->Y1 * scale;
            const float localX[4] = {x0 - originX, x1 - originX, x1 - originX, x0 - originX};
            const float localY[4] = {y0 - originY, y0 - originY, y1 - originY, y1 - originY};
            ImU32 colors[4];
            for (int c = 0; c < 4; ++c)
            {
                const float projection =
                    localX[c] * dirX + localY[c] * dirY;
                const float cornerPhase =
                    ((projection - projectionMin) / projectionRange) * cycles;
                colors[c] = detail::packColor(
                    detail::animatedThemeColor(cornerPhase, time), alpha);
            }

            drawList->PrimReserve(6, 4);
            const ImDrawIdx base = static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx);
            drawList->PrimWriteVtx(ImVec2(x0, y0), ImVec2(glyph->U0, glyph->V0), colors[0]);
            drawList->PrimWriteVtx(ImVec2(x1, y0), ImVec2(glyph->U1, glyph->V0), colors[1]);
            drawList->PrimWriteVtx(ImVec2(x1, y1), ImVec2(glyph->U1, glyph->V1), colors[2]);
            drawList->PrimWriteVtx(ImVec2(x0, y1), ImVec2(glyph->U0, glyph->V1), colors[3]);
            drawList->PrimWriteIdx(base);
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1));
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(base + 2));
            drawList->PrimWriteIdx(base);
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(base + 2));
            drawList->PrimWriteIdx(static_cast<ImDrawIdx>(base + 3));
        }

        x += glyph->AdvanceX * scale;
        p = next;
    }
    drawList->PopTexture();

    return ImVec2(maxLineWidth, textHeight);
}

} // namespace mc::features
