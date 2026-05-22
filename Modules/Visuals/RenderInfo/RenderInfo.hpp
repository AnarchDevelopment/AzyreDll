/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <cstdint>
#include <windows.h>
#include "../../../ImGui/imgui.h"

// Forward declarations
class ImDrawList;
struct ImVec2;
class HudElement;

// RenderInfo Module
class RenderInfo {
public:
    static bool g_showRenderInfo;
    static float g_fpsCounter;
    static float g_frameTime;
    static ULONGLONG g_renderInfoEnableTime;
    static ULONGLONG g_renderInfoDisableTime;
    static float g_renderInfoAnim;
    static HudElement* g_renderInfoHud;
    
    // Settings
    static bool g_showBackground;
    static bool g_showGlow;
    static float g_bgOpacity;
    static ImVec4 g_staticColor;
    static float g_scale;

    // Initialize renderinfo module
    static void Initialize(HudElement* hud);

    // Update animation state
    static void UpdateAnimation(ULONGLONG now);
    
    // Global FPS counter update
    static void UpdateFPS();

    // Render renderinfo in array list
    static void RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd);

    // Render renderinfo window
    static void RenderWindow();

    // Render renderinfo UI in menu
    static void RenderMenu();

    // Check if renderinfo is enabled
    static bool IsEnabled() { return g_showRenderInfo; }
};
