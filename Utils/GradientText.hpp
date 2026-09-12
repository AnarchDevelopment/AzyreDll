/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include "../ImGui/imgui.h"
#include <vector>
#include <cmath>
#include <cstring>

namespace GradientText {
    // Adjust saturation: 0 = white (pastel), 1 = original color
    inline ImVec4 AdjustSaturation(const ImVec4& col, float saturation) {
        return ImVec4(
            1.0f + (col.x - 1.0f) * saturation,
            1.0f + (col.y - 1.0f) * saturation,
            1.0f + (col.z - 1.0f) * saturation,
            col.w
        );
    }

    // Multi-stop color interpolation with seamless wrapping: t in [0,1] cycles through colors
    inline ImVec4 GetInterpolatedColor(const std::vector<ImVec4>& colors, float t) {
        if (colors.empty()) return ImVec4(1, 1, 1, 1);
        if (colors.size() == 1) return colors[0];

        // Wrap t smoothly
        t = t - floorf(t);

        float scaledT = t * (float)colors.size();
        int idx1 = (int)scaledT;
        int idx2 = (idx1 + 1) % (int)colors.size();
        float blend = scaledT - idx1;

        const ImVec4& c1 = colors[idx1];
        const ImVec4& c2 = colors[idx2];

        return ImVec4(
            c1.x + (c2.x - c1.x) * blend,
            c1.y + (c2.y - c1.y) * blend,
            c1.z + (c2.z - c1.z) * blend,
            c1.w + (c2.w - c1.w) * blend
        );
    }

    // Pre-compute a 256-entry color lookup table from the gradient colors + animation offset
    inline void BuildColorLUT(const std::vector<ImVec4>& colors, float timeOffset,
                              float saturation, ImU32 outLUT[256]) {
        for (int i = 0; i < 256; i++) {
            float t = (float)i / 255.0f + timeOffset;
            // Wrap smoothly without fmodf discontinuity by using the fractional part
            t = t - floorf(t);
            ImVec4 col = GetInterpolatedColor(colors, t);
            col = AdjustSaturation(col, saturation);
            outLUT[i] = ImGui::GetColorU32(col);
        }
    }

    // Draw text with vertex-based gradient at a given angle
    // Uses a pre-computed 256-entry color LUT for flicker-free rendering
    // linear = true: gradient scrolls continuously in one direction
    // linear = false: gradient oscillates back and forth
    inline void DrawGradientText(ImDrawList* draw, ImFont* font, float fontSize,
                                 ImVec2 pos, const char* text,
                                 const std::vector<ImVec4>& colors,
                                 float alpha, float speed = 1.0f,
                                 float angleDeg = 0.0f,
                                 float saturation = 1.0f,
                                 bool linear = true) {
        if (!text || text[0] == '\0') return;
        if (colors.empty()) return;

        // Measure text size
        ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
        if (textSize.x <= 0.0f || textSize.y <= 0.0f) return;

        // Record vertex buffer start before drawing
        int vtx_start = draw->VtxBuffer.Size;

        // Draw text with white base to preserve alpha channel from font rasterization
        draw->AddText(font, fontSize, pos, IM_COL32(255, 255, 255, (int)(alpha * 255)), text);

        int vtx_end = draw->VtxBuffer.Size;
        if (vtx_end <= vtx_start) return;

        // Pre-compute gradient direction from angle
        float angleRad = angleDeg * 3.14159265f / 180.0f;
        float dirX = cosf(angleRad);
        float dirY = sinf(angleRad);

        // Gradient spans the full diagonal of the text bounding box
        float halfW = textSize.x * 0.5f;
        float halfH = textSize.y * 0.5f;
        float maxProj = fabsf(dirX * halfW) + fabsf(dirY * halfH);
        if (maxProj < 0.001f) maxProj = 1.0f;

        // Time offset: linear scrolls continuously, oscillating uses sine
        float timeOffset;
        if (linear) {
            timeOffset = fmodf((float)GetTickCount64() / 1000.0f * speed, 1.0f);
        } else {
            timeOffset = sinf((float)GetTickCount64() / 1000.0f * speed) * 0.35f;
        }

        // Pre-compute the color LUT (256 entries, one-time per call)
        ImU32 colorLUT[256];
        BuildColorLUT(colors, timeOffset, saturation, colorLUT);

        // Top-left corner as reference origin
        float originX = pos.x;
        float originY = pos.y;

        // For each vertex, sample the pre-computed LUT
        for (int i = vtx_start; i < vtx_end; i++) {
            ImDrawVert& vert = draw->VtxBuffer[i];

            // Vector from text origin to vertex
            float vx = vert.pos.x - originX;
            float vy = vert.pos.y - originY;

            // Project onto gradient direction and normalize to [0, 255]
            float proj = vx * dirX + vy * dirY;
            int lutIdx = (int)((proj / (maxProj * 2.0f) + 0.5f) * 255.0f + 0.5f);
            if (lutIdx < 0) lutIdx = 0;
            if (lutIdx > 255) lutIdx = 255;

            // Sample pre-computed color (no per-vertex interpolation)
            ImU32 gradCol32 = colorLUT[lutIdx];

            // Extract RGB from LUT, keep original alpha from font rasterization
            float origAlpha = ImGui::ColorConvertU32ToFloat4(vert.col).w;
            float r = (float)((gradCol32 >> 0) & 0xFF) / 255.0f;
            float g = (float)((gradCol32 >> 8) & 0xFF) / 255.0f;
            float b = (float)((gradCol32 >> 16) & 0xFF) / 255.0f;

            vert.col = ImGui::GetColorU32(ImVec4(r, g, b, origAlpha * alpha));
        }
    }
}
