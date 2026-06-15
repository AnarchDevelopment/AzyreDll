/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <string>
#include <vector>
#include <windows.h>

// Forward declarations
class ImDrawList;
struct ImVec2;
struct ImVec4;
class HudElement;

class CPSCounter {
public:
    // Configuration
    static bool g_showCpsCounter;
    static std::string g_cpsCounterFormat;
    static float g_cpsTextScale;
    static ImVec4 g_cpsTextColor;
    
    // Animation
    static float g_cpsCounterAnim;
    static ULONGLONG g_cpsCounterEnableTime;
    static ULONGLONG g_cpsCounterDisableTime;
    
    // Display options
    static int g_cpsCounterAlignment;
    static bool g_cpsCounterShadow;
    static float g_cpsCounterShadowOffset;
    static ImVec4 g_cpsCounterShadowColor;
    static float g_cpsCounterX;
    static float g_cpsCounterY;
    static bool g_cpsCounterFirstRender;
    
    // CPS Click tracking
    static const int MAX_CPS_HISTORY = 100;
    static ULONGLONG g_lmbClickTimes[100];
    static ULONGLONG g_rmbClickTimes[100];
    static int g_lmbClickIndex;
    static int g_rmbClickIndex;
    static int g_lmbCps;
    static int g_rmbCps;
    
    // HUD Element
    static HudElement* g_cpsHud;
    
    // Methods
    static void Initialize(HudElement* hudElement);
    static void UpdateAnimation(ULONGLONG now);
    static void UpdateCPS(ULONGLONG now, bool lmbPressed, bool rmbPressed, bool g_prevLmbPressed, bool g_prevRmbPressed);
    static void RenderArrayList(class ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd);
    static void RenderDisplay(int screenWidth, int screenHeight);
    static void RenderMenu();
    
    // Helper
    static std::string ProcessCPSCounterFormat(const std::string& format, int lmb, int rmb);
};
