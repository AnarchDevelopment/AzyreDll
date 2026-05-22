/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

class Animations {
public:
    // === Easing Functions ===
    // Cubic easing (smooth in/out)
    static float SmoothInertia(float t);
    
    // Quadratic easing (in/out)
    static float EaseInOutQuad(float t);
    
    // Exponential easing (out)
    static float EaseOutExpo(float t);
    
    // Quartic easing
    static float EaseInQuart(float t);
    static float EaseOutQuart(float t);
    
    // Back easing (overshoot)
    static float EaseOutBack(float t);
    
    // Elastic bounce effect (in/out)
    static float EaseInOutElastic(float t);
    
    // Linear (no easing)
    static float Linear(float t);
    
    // === Animation Utilities ===
    // Calculate progress from time with duration
    static float GetProgress(float elapsed, float duration);
    
    // Clamp value between 0 and 1
    static float Clamp01(float value);
    
    // Frame-rate independent approach (damped spring-like lerp)
    static float Approach(float current, float target, float dt, float speed);
};
