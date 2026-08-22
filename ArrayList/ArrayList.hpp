/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include "../ImGui/imgui.h"
#include <d3d11.h>
#include <string>
#include <vector>
#include <map>
#include "../Utils/HudElement.hpp"

namespace ArrayList {
    struct ModuleInfo {
        std::string name;
        std::string suffix;
        bool enabled;
        float animation; // 0.0 to 1.0
        float width;
    };

    // Module Settings
    extern bool g_enabled;
    extern ImVec4 g_bgColor;
    extern float g_bgOpacity;
    extern bool g_showSideBar;
    extern ImVec4 g_sideBarColor;
    extern bool g_chromaSideBar;
    extern bool g_roundedBorders;
    extern float g_borderRadius;
    extern bool g_showSuffix;
    extern std::string g_fontName;
    extern float g_size;

    // Text customization
    extern bool g_chromaText;
    extern ImVec4 g_textColor;
    extern ImVec4 g_suffixColor;
    extern float g_chromaSpeed;

    // Glow
    extern bool g_glowEnabled;
    extern float g_glowStrength;

    // Animation style: 0 = Slide, 1 = Fade, 2 = Stagger
    extern int g_animationStyle;

    // Side mode: 0 = Auto, 1 = Left, 2 = Right
    extern int g_sideMode;

    // Background: 0 = Normal, 1 = Mica Blur (frosted glass behind the rows)
    extern int g_backgroundMode;
    extern float g_blurRadius;
    extern float g_blurOpacity;

    // Extra settings
    extern float g_rowSpacing;
    extern float g_animationSpeed;
    extern float g_sideBarWidth;
    extern bool g_showBorder;
    extern ImVec4 g_borderColor;
    extern float g_borderWidth;
    extern bool g_textShadow;
    extern float g_textShadowOffset;

    // Region blur integration (last drawn bounds, filled by Render each frame)
    extern bool g_hasBlurRect;
    extern float g_blurRectX;
    extern float g_blurRectY;
    extern float g_blurRectW;
    extern float g_blurRectH;

    // Main render function
    void Render();

    // Draggable HUD support
    void HandleHudDrag(float screenWidth, bool menuOpen);

    // Mica blur background for the ArrayList rect (called from the present hook)
    void RenderBlur(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, IDXGISwapChain* pSwapChain);

    // Menu rendering for Visuals tab
    void RenderMenu();

    // Internal state management
    void UpdateModules();

    // Draggable HUD support
    extern HudElement* g_hud;
}
