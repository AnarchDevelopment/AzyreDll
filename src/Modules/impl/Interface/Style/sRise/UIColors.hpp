#pragma once

#include <imgui.h>

namespace mc::gui::sRise::colors {

// Paleta oficial de Rise 6.9.5 (UIColors.java):
// BACKGROUND:     (23, 26, 33, 254) -> #171A21
// SECONDARY:      (18, 20, 25, 255) -> #121419
// TEXT:           (255, 255, 255, 255)
// SECONDARY_TEXT: (255, 255, 255, 220)
// TRINARY_TEXT:   (255, 255, 255, 130)
// OVERLAY:        (0, 0, 0, 50)

inline ImU32 background(float a = 1.0f)
{
    return IM_COL32(23, 26, 33, (int)(254.0f * a));
}

inline ImU32 secondary(float a = 1.0f)
{
    return IM_COL32(18, 20, 25, (int)(255.0f * a));
}

inline ImU32 text(float a = 1.0f)
{
    return IM_COL32(255, 255, 255, (int)(255.0f * a));
}

inline ImU32 secondaryText(float a = 1.0f)
{
    return IM_COL32(255, 255, 255, (int)(220.0f * a));
}

inline ImU32 trinaryText(float a = 1.0f)
{
    return IM_COL32(255, 255, 255, (int)(130.0f * a));
}

inline ImU32 overlay(float a = 1.0f)
{
    return IM_COL32(0, 0, 0, (int)(50.0f * a));
}

inline ImU32 cardHover(float a = 1.0f)
{
    return IM_COL32(0, 0, 0, (int)(35.0f * a));
}

} // namespace mc::gui::sRise::colors
