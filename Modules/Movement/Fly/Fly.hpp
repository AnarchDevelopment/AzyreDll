/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <cstdint>
#include <windows.h>

struct ImDrawList;
struct ImVec2;

// Fly Module
class Fly {
public:
    static bool g_flyEnabled;
    static bool g_flyPatched;          // True while the injection is applied
    static uintptr_t g_flyAddr;        // Injection point: cmp byte ptr [rcx+00000E31], 00
    static void* g_flyCave;
    static BYTE g_flyBackup[7];        // Backup of the original instruction bytes
    static ULONGLONG g_flyEnableTime;
    static ULONGLONG g_flyDisableTime;

    // Initialize the module
    static void Initialize(uintptr_t gameBase);

    // Scan the game module for the fly patch target
    static void ScanPattern(uintptr_t gameBase, size_t imageSize);

    // Enable/Disable fly
    static void Enable();
    static void Disable();

    // Restore original memory on unload
    static void Shutdown();

    // Render fly UI in menu
    static void RenderMenu();

    // Check if fly is enabled
    static bool IsEnabled() { return g_flyEnabled; }
};
