/*
Under an4rch Development Public Source License 1.0
*/

#include "Animations.hpp"
#include <cmath>
#include <algorithm>

// === Easing Functions ===

float Animations::SmoothInertia(float t) {
    // Cubic easing for smooth in/out
    if (t < 0.5f) {
        return 4.0f * t * t * t;
    } else {
        float f = 2.0f * t - 2.0f;
        return 0.5f * f * f * f + 1.0f;
    }
}

float Animations::EaseInOutQuad(float t) {
    // Quadratic easing (parabolic)
    return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
}

float Animations::EaseOutExpo(float t) {
    // Exponential easing (decelerating)
    return t == 1.0f ? 1.0f : 1.0f - std::powf(2.0f, -10.0f * t);
}

float Animations::EaseInQuart(float t) {
    return t * t * t * t;
}

float Animations::EaseOutQuart(float t) {
    return 1.0f - std::powf(1.0f - t, 4.0f);
}

float Animations::EaseOutBack(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * std::powf(t - 1.0f, 3.0f) + c1 * std::powf(t - 1.0f, 2.0f);
}

float Animations::EaseInOutElastic(float t) {
    // Elastic bounce effect
    const float c5 = (2.0f * 3.14159265f) / 4.5f;
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    return t < 0.5f 
        ? -(std::powf(2.0f, 20.0f * t - 10.0f) * std::sinf((t * 2.0f - 0.675f) * c5)) / 2.0f
        : (std::powf(2.0f, -20.0f * t + 10.0f) * std::sinf((t * 2.0f - 0.675f) * c5)) / 2.0f + 1.0f;
}

float Animations::Linear(float t) {
    // No easing, linear interpolation
    return t;
}

// === Animation Utilities ===

float Animations::GetProgress(float elapsed, float duration) {
    // Calculate progress from elapsed time and total duration (0.0 to 1.0)
    if (duration <= 0.0f) return 1.0f;
    return std::min(elapsed / duration, 1.0f);
}

float Animations::Clamp01(float value) {
    // Clamp value between 0 and 1
    return std::max(0.0f, std::min(value, 1.0f));
}

float Animations::Approach(float current, float target, float dt, float speed) {
    return current + (target - current) * (1.0f - std::expf(-speed * dt));
}
