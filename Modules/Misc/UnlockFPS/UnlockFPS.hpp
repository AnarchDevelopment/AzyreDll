/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <windows.h>

// Forward declarations
class ImDrawList;
struct ImVec2;

/// @brief UnlockFPS module - Unlocks frame rate and provides FPS limiting
class UnlockFPS {
public:
    // Static member variables for state
    static bool g_unlockFpsEnabled;
    static float g_fpsLimit;
    static ULONGLONG g_unlockFpsEnableTime;
    static ULONGLONG g_unlockFpsDisableTime;
    
    // Methods
    static void Initialize();
    static void UpdateFPS();
    static void RenderArrayList(class ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd);
    static void RenderMenu();
    static void SetFPS(float fps);
    static float GetFPS();
};
