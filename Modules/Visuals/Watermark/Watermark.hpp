/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <windows.h>
#include <vector>
#include "../../../ImGui/imgui.h" // Need ImVec4 definition too if it's not a forward decl


// Forward declarations
class ImDrawList;
struct ImVec2;
struct ImVec4;
class HudElement;

/// @brief Watermark module - Displays "Aegleseeker" branding with chroma effect
class Watermark {
public:
    // Static member variables
    static bool g_showWatermark;
    static ULONGLONG g_watermarkEnableTime;
    static ULONGLONG g_watermarkDisableTime;
    static float g_watermarkAnim;
    static HudElement* g_watermarkHud;

    // Settings
    static bool g_useImage;
    static char g_customText[128];
    static bool g_showGlow;
    static bool g_chromaText;
    static ImVec4 g_staticColor;
    static float g_fontSize;
    static float g_bgOpacity;
    static bool g_showBackground;
    static bool g_showShimmer;
    static float g_chromaSpeed;
    static bool g_chromaDirection; // true = forward, false = backward
    static bool g_mirroredGradient;
    static bool g_edgeFade;
    
    // Advanced Customization
    static std::vector<ImVec4> g_chromaColors;
    static float g_imageOpacity;
    static float g_imageSize;

    // Texture Data
    static void* g_watermarkTexture;
    static int g_texWidth;
    static int g_texHeight;

    /// @brief Initialize Watermark with HudElement reference
    static void Initialize(HudElement* hud);

    /// @brief Initialize textures from resources
    static bool InitializeTextures();

    /// @brief Release resources
    static void Shutdown();

    /// @brief Update animation state (call from main render loop)
    static void UpdateAnimation(ULONGLONG now);

    /// @brief Render watermark in array list (for HUD display)
    static void RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd);

    /// @brief Render watermark display
    static void RenderDisplay();

    /// @brief Render menu controls
    static void RenderMenu();
};
