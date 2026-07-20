/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <windows.h>
#include <string>
#include "ImGui/imgui.h"

// Forward declarations
struct ImDrawList;
struct ImVec2;
struct HudElement;

/// @brief Ping Counter module - Simulated RakNet latency counter
class PingCounter {
public:
    static bool g_showPingCounter;
    static HudElement* g_pingHud;
    
    // Animation state
    static float g_pingAnim;
    static ULONGLONG g_pingEnableTime;
    static ULONGLONG g_pingDisableTime;

    // Config options
    static float g_pingTextScale;
    static bool g_showBackground;
    static float g_bgOpacity;
    static ImVec4 g_pingTextColor;
    static ImVec4 g_pingCounterShadowColor;
    static bool g_pingTextShadow;
    static std::string g_fontName;

    // Ping generation parameters
    static int g_minPing;
    static int g_maxPing;
    static int g_currentPing;
    static ULONGLONG g_lastPingUpdate;
    static int g_pingUpdateInterval; // ms

    static void Initialize(HudElement* hud);
    static void UpdateAnimation(ULONGLONG now);
    static void RenderDisplay(float sw, float sh);
    static void RenderMenu();
    static void UpdatePing(ULONGLONG now);
};
