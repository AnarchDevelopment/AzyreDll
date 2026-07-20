/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include "../ImGui/imgui.h"
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

    // Main render function
    void Render();
    
    // Menu rendering for Visuals tab
    void RenderMenu();
    
    // Internal state management
    void UpdateModules();

    // Draggable HUD support
    extern HudElement* g_hud;
}
