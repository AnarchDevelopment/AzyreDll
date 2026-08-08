/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <windows.h>
#include <vector>
#include <string>
#include "imgui.h"

// Forward declarations
struct ImDrawList;
struct ImVec2;
struct HudElement;

/// @brief Keystrokes module - Displays pressed keys (WASD, Mouse, Spacebar) with visual feedback
class Keystrokes {
public:
    // Static member variables for state
    static bool g_showKeystrokes;
    static float g_keystrokesAnim;
    static ULONGLONG g_keystrokesEnableTime;
    static ULONGLONG g_keystrokesDisableTime;
    static HudElement* g_keystrokesHud;

    // Keystrokes config
    static float g_keystrokesUIScale;
    static bool g_keystrokesBlurEffect;
    static float g_keystrokesRounding;
    static bool g_keystrokesShowBg;
    static bool g_keystrokesRectShadow;
    static float g_keystrokesRectShadowOffset;
    static bool g_keystrokesBorder;
    static float g_keystrokesBorderWidth;
    static bool g_keystrokesGlow;
    static float g_keystrokesGlowAmount;
    static bool g_keystrokesGlowEnabled;
    static float g_keystrokesGlowEnabledAmount;
    static float g_keystrokesGlowSpeed;
    static float g_keystrokesKeySpacing;
    static float g_keystrokesEdSpeed;
    static float g_keystrokesTextScale;
    static float g_keystrokesTextScale2;
    static float g_keystrokesTextXOffset;
    static float g_keystrokesTextYOffset;
    static bool g_keystrokesTextShadow;
    static float g_keystrokesTextShadowOffset;
    static bool g_keystrokesLMBRMB;
    static bool g_keystrokesHideCPS;
    static std::string g_keystrokesWText;
    static std::string g_keystrokesAText;
    static std::string g_keystrokesSText;
    static std::string g_keystrokesDText;
    static std::string g_keystrokesLMBText;
    static std::string g_keystrokesRMBText;
    static std::string g_keystrokesLMBCPSText;
    static std::string g_keystrokesRMBCPSText;
    static std::string g_fontName;

    // Keystrokes colors
    static ImVec4 g_keystrokesBgColor;
    static ImVec4 g_keystrokesEnabledColor;
    static ImVec4 g_keystrokesTextColor;
    static ImVec4 g_keystrokesTextEnabledColor;
    static ImVec4 g_keystrokesRectShadowColor;
    static ImVec4 g_keystrokesTextShadowColor;
    static ImVec4 g_keystrokesBorderColor;
    static ImVec4 g_keystrokesGlowColor;
    static ImVec4 g_keystrokesGlowEnabledColor;
    static ImVec4 g_keystrokesEnabledShadowColor;
    static ImVec4 g_keystrokesDisabledShadowColor;

    // Keystrokes state colors
    static std::vector<ImVec4> g_keystrokesStates;
    static std::vector<ImVec4> g_keystrokesTextStates;
    static std::vector<ImVec4> g_keystrokesShadowStates;
    static float g_keystrokesGlowModifier[7];

    // Additional keystrokes config
    static bool g_keystrokesShowSpacebar;
    static float g_keystrokesSpacebarWidth;
    static float g_keystrokesSpacebarHeight;
    static bool g_keystrokesShowMouseButtons;
    static bool g_keystrokesShowLMBRMB;
    static std::string g_keystrokesLMBFormatText;
    static std::string g_keystrokesRMBFormatText;

    // CPS tracking
    static std::vector<ULONGLONG> g_lmbClickTimes;
    static std::vector<ULONGLONG> g_rmbClickTimes;
    static int g_lmbClickIndex;
    static int g_rmbClickIndex;
    static int g_lmbCps;
    static int g_rmbCps;
    static bool g_prevLmbPressed;
    static bool g_prevRmbPressed;

    /// @brief Initialize Keystrokes with HudElement reference
    static void Initialize(HudElement* hud);

    /// @brief Update animation state (call from main render loop)
    static void UpdateAnimation(ULONGLONG now);

    /// @brief Render keystrokes in array list (for HUD display)
    static void RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd);

    /// @brief Render keystrokes display
    static void RenderDisplay(float sw, float sh);

    /// @brief Process keystrokes format string
    static std::string ProcessKeystrokesFormat(const std::string& format, int value);

    /// @brief Render menu controls for keystrokes settings
    static void RenderMenu();
};
