/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include "../../../../ImGui/imgui.h"
#include <cmath>

// ImVec4 lerp helper
inline ImVec4 LerpImVec4(ImVec4 a, ImVec4 b, float t) {
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t
    );
}

// Smooth inertia easing
inline float SmoothInertia(float t) {
    return t * t * (3.0f - 2.0f * t);
}

// Exponential ease out
inline float EaseOutExpo(float t) {
    if (t >= 1.0f) return 1.0f;
    return 1.0f - powf(2.0f, -10.0f * t);
}
