/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <cstdint>

// Startup splash screen rendered at injection time: black background, centered
// white watermark, then a cinematic slide-up with a loading bar before the
// main client is revealed.
class Splash {
public:
    // Creates the white logo texture (render thread, called lazily by Begin)
    static void Initialize();

    // Starts the splash timeline. Called right after the client finishes
    // loading assets/offsets on the first presented frame.
    static void Begin();

    // True while the splash is still occupying the screen.
    static bool IsActive();

    // Advances the timeline (render thread, once per frame).
    static void Update(unsigned long long now);

    // Draws the full-screen overlay.
    static void Render(float sw, float sh);

    // Releases the logo texture on unload.
    static void Shutdown();
};
