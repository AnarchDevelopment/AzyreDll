/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <cstdint>
#include <windows.h>

// Forward declarations
class ImDrawList;
struct ImVec2;

// Reach Module
class Reach {
public:
    static float* g_reachPtr;
    static float g_reachValue;
    static bool g_reachEnabled;
    static ULONGLONG g_reachEnableTime;
    static ULONGLONG g_reachDisableTime;

    // Initialize reach module
    static void Initialize(uintptr_t gameBase);

    // Update reach value in memory
    static void UpdateValue(float newValue);

    // Enable or disable reach module
    static void SetEnabled(bool enabled);

    // Render reach in array list
    static void RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd);

    // Render reach UI in menu
    static void RenderMenu();

    // Check if reach is enabled
    static bool IsEnabled() { return g_reachEnabled; }
};
