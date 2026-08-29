/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <windows.h>
#include <vector>
#include <string>
#include "../../../ImGui/imgui.h"
#include "../../../Utils/HudElement.hpp"

/// @brief MouseStrokes module - Visualizes camera and mouse movement in real time
class MouseStrokes {
public:
    struct CircleTrail {
        float x, y;
        float alpha;
    };

    // State & Lifecycle
    static bool g_showMouseStrokes;
    static float g_mouseStrokesAnim;
    static ULONGLONG g_mouseStrokesEnableTime;
    static ULONGLONG g_mouseStrokesDisableTime;
    static HudElement* g_mouseStrokesHud;

    // Movement & Coordinates
    static float g_accumulatedX;
    static float g_accumulatedY;
    static ImVec2 g_currentCursorPos;
    static std::vector<CircleTrail> g_trails;
    static POINT g_lastMousePoint;
    static bool g_hasLastPoint;

    // Settings
    static float g_uiScale;
    static float g_boxSize;
    static float g_rounding;
    static bool g_showBackground;
    static ImVec4 g_bgColor;
    static bool g_showBorder;
    static ImVec4 g_borderColor;
    static float g_borderWidth;
    static bool g_showShadow;
    static ImVec4 g_shadowColor;
    static bool g_showGlow;
    static ImVec4 g_glowColor;
    static float g_glowAmount;
    static ImVec4 g_cursorColor;
    static float g_dotRadius;
    static float g_sensitivity;
    static float g_decayRate;
    static float g_smoothSpeed;
    static bool g_showCrosshair;
    static ImVec4 g_crosshairColor;
    static bool g_clickEffect;
    static ImVec4 g_clickColor;

    // Methods
    static void Initialize(HudElement* hud);
    static void UpdateAnimation(ULONGLONG now);
    static void UpdateMovement(float dt);
    static void OnRawMouseInput(int dx, int dy);
    static void RenderDisplay(float screenWidth, float screenHeight);
    static void RenderMenu();
};
