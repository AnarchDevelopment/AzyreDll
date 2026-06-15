/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <string>
#include <windows.h>
#include "../../../ImGui/imgui.h"

// Forward declarations
class ImDrawList;
struct ImVec2;
class HudElement;

class FPSOverlay {
public:
    // Configuration
    static bool g_showFpsOverlay;
    static float g_fpsTextScale;
    static ImVec4 g_fpsTextColor;
    static bool g_showBackground;
    static float g_bgOpacity;
    static bool g_showShadow;
    static float g_shadowSpread;
    static float g_shadowBlur;
    static bool g_showTextShadow;
    static float g_textShadowOffset;
    static ImVec4 g_accentColor;
    
    // Animation
    static float g_fpsOverlayAnim;
    static ULONGLONG g_fpsOverlayEnableTime;
    static ULONGLONG g_fpsOverlayDisableTime;
    
    // HUD Element
    static HudElement* g_fpsHud;
    
    // Methods
    static void Initialize(HudElement* hudElement);
    static void UpdateAnimation(ULONGLONG now);
    static void RenderArrayList(class ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd);
    static void RenderDisplay(int screenWidth, int screenHeight);
    static void RenderMenu();
};
