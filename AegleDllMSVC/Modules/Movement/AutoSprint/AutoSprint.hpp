/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <cstdint>
#include <windows.h>

struct ImDrawList;
struct ImVec2;

// AutoSprint Module
class AutoSprint {
public:
    static bool g_autoSprintEnabled;
    static uintptr_t g_autoSprintAddr;
    static void* g_autoSprintCave;
    static BYTE g_autoSprintBackup[11];
    static ULONGLONG g_autoSprintEnableTime;
    static ULONGLONG g_autoSprintDisableTime;

    // Initialize autosprint module
    static void Initialize(uintptr_t gameBase);

    // Scan the game module for the autosprint patch target
    static void ScanPattern(uintptr_t gameBase, size_t imageSize);

    // Enable/Disable autosprint
    static void Enable();
    static void Disable();

    // Render autosprint in array list
    static void RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd);

    // Render autosprint UI in menu
    static void RenderMenu();

    // Check if autosprint is enabled
    static bool IsEnabled() { return g_autoSprintEnabled; }

    // Pattern scanning utility
    static uintptr_t PatternScan(uintptr_t start, size_t size, const BYTE* pattern, size_t patternSize);
};
