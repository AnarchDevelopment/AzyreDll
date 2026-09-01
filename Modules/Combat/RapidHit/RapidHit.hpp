/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <cstdint>
#include <windows.h>

// Forward declarations
struct ImDrawList;
struct ImVec2;

// RapidHit Module
class RapidHit {
public:
    static bool g_rapidHitEnabled;
    static uintptr_t g_rapidHitAddr;
    static void* g_rapidHitCave;
    static BYTE g_rapidHitBackup[8];
    static ULONGLONG g_rapidHitEnableTime;
    static ULONGLONG g_rapidHitDisableTime;

    // Initialize rapid hit module
    static void Initialize(uintptr_t gameBase);

    // Scan the game module for the rapid hit patch target
    static void ScanPattern(uintptr_t gameBase, size_t imageSize);

    // Enable/Disable rapid hit
    static void Enable();
    static void Disable();

    // Render rapid hit in array list
    static void RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd);

    // Render rapid hit UI in menu
    static void RenderMenu();

    // Check if rapid hit is enabled
    static bool IsEnabled() { return g_rapidHitEnabled; }
};
