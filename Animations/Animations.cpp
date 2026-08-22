/*
Under an4rch Development Public Source License 1.0
*/

#include "Animations.hpp"
#include <cmath>
#include <algorithm>

namespace {
    float NormalizeProgress(float t) {
        return std::isfinite(t) ? std::clamp(t, 0.0f, 1.0f) : 0.0f;
    }
}

// === Easing Functions ===

float Animations::SmoothInertia(float t) {
    t = NormalizeProgress(t);
    if (t < 0.5f) {
        return 4.0f * t * t * t;
    } else {
        float f = 2.0f * t - 2.0f;
        return 0.5f * f * f * f + 1.0f;
    }
}

float Animations::EaseInOutQuad(float t) {
    t = NormalizeProgress(t);
    return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
}

float Animations::EaseOutExpo(float t) {
    t = NormalizeProgress(t);
    return t == 1.0f ? 1.0f : 1.0f - std::powf(2.0f, -10.0f * t);
}

float Animations::EaseInQuart(float t) {
    t = NormalizeProgress(t);
    return t * t * t * t;
}

float Animations::EaseOutQuart(float t) {
    t = NormalizeProgress(t);
    return 1.0f - std::powf(1.0f - t, 4.0f);
}

float Animations::EaseOutBack(float t) {
    t = NormalizeProgress(t);
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    return 1.0f + c3 * std::powf(t - 1.0f, 3.0f) + c1 * std::powf(t - 1.0f, 2.0f);
}

float Animations::EaseInOutElastic(float t) {
    t = NormalizeProgress(t);
    const float c5 = (2.0f * 3.14159265f) / 4.5f;
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    return t < 0.5f 
        ? -(std::powf(2.0f, 20.0f * t - 10.0f) * std::sinf((t * 2.0f - 0.675f) * c5)) / 2.0f
        : (std::powf(2.0f, -20.0f * t + 10.0f) * std::sinf((t * 2.0f - 0.675f) * c5)) / 2.0f + 1.0f;
}

float Animations::Linear(float t) {
    return NormalizeProgress(t);
}

// === Animation Utilities ===

float Animations::GetProgress(float elapsed, float duration) {
    if (!std::isfinite(elapsed) || !std::isfinite(duration) || duration <= 0.0f) {
        return duration > 0.0f ? 0.0f : 1.0f;
    }
    return NormalizeProgress(elapsed / duration);
}

float Animations::Clamp01(float value) {
    return NormalizeProgress(value);
}

float Animations::Approach(float current, float target, float dt, float speed) {
    if (!std::isfinite(current) || !std::isfinite(target)) return target;
    if (!std::isfinite(dt) || !std::isfinite(speed) || dt <= 0.0f || speed <= 0.0f) return current;

    const float factor = 1.0f - std::expf(-speed * std::min(dt, 0.25f));
    const float next = current + (target - current) * factor;
    return std::fabs(target - next) < 0.0001f ? target : next;
}
