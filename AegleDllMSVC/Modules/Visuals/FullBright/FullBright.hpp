/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <cstdint>
#include <windows.h>

struct ImDrawList;
struct ImVec2;

// FullBright Module
class FullBright {
public:
    static bool g_fullBrightEnabled;
    static float g_fullBrightValue;
    static uintptr_t g_fullBrightAddr;
    static void* g_fullBrightCave;
    static BYTE g_fullBrightBackup[8];
    static ULONGLONG g_fullBrightEnableTime;
    static ULONGLONG g_fullBrightDisableTime;

    // Initialize fullbright module
    static void Initialize(uintptr_t gameBase);

    // Scan the game module for the fullbright patch target
    static void ScanPattern(uintptr_t gameBase, size_t imageSize);

    // Enable/Disable fullbright
    static void Enable();
    static void Disable();

    // Render fullbright in array list
    static void RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd);

    // Render fullbright UI in menu
    static void RenderMenu();

    // Check if fullbright is enabled
    static bool IsEnabled() { return g_fullBrightEnabled; }
};
