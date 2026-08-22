/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <cstdint>
#include <windows.h>

struct ImDrawList;
struct ImVec2;

// Glide Module
class Glide {
public:
    static bool g_glideEnabled;
    static bool g_glidePatched;          // True while the injection is applied
    static float g_glideSpeed;           // Vertical velocity applied while falling (negative = slow fall)
    static uintptr_t g_glideAddr;        // Injection point: movss [rdi+34], xmm0
    static void* g_glideCave;
    static int g_glideSpeedOffset;       // Offset of the speed immediate inside the cave
    static BYTE g_glideBackup[5];        // Backup of the original instruction bytes
    static ULONGLONG g_glideEnableTime;
    static ULONGLONG g_glideDisableTime;

    // Initialize the module
    static void Initialize(uintptr_t gameBase);

    // Scan the game module for the glide patch target
    static void ScanPattern(uintptr_t gameBase, size_t imageSize);

    // Enable/Disable glide
    static void Enable();
    static void Disable();

    // Restore original memory on unload
    static void Shutdown();

    // Patch the speed immediate into the shellcode while enabled
    static void UpdateSpeed();

    // Render glide UI in menu
    static void RenderMenu();

    // Check if glide is enabled
    static bool IsEnabled() { return g_glideEnabled; }
};
