/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <cstdint>
#include <windows.h>

struct ImDrawList;
struct ImVec2;

// HighJump Module
class HighJump {
public:
    static bool g_enabled;
    static float g_jumpValue;
    static uintptr_t g_addr;
    static void* g_cave;
    static float* g_jumpValuePtr;
    static BYTE g_backup[5];
    static ULONGLONG g_enableTime;
    static ULONGLONG g_disableTime;

    static void Initialize(uintptr_t gameBase);
    static void ScanPattern(uintptr_t gameBase, size_t imageSize);
    static void Enable();
    static void Disable();
    static void RenderMenu();
    static bool IsEnabled() { return g_enabled; }
};
