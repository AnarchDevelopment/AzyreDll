/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <windows.h>
#include <map>
#include <string>
#include <vector>
#include "../ImGui/imgui.h"

/// @brief GUI class - Handles all UI logic and rendering
class GUI {
public:
    // Menu state
    static bool g_showMenu;
    static float g_menuAnim;
    
    // Tab state
    static int g_currentTab;
    static int g_previousTab;
    static ULONGLONG g_tabChangeTime;
    static float g_tabAnim;
    static float g_ircShiftAnim;
    
    // Animation states for UI elements
    static std::map<std::string, float> g_elementAnims;
    
    // Theme configurations
    enum ThemePreset {
        Theme_AegleClassic,
        Theme_SakuraBlossom,
        Theme_Cyberpunk,
        Theme_EmeraldForest,
        Theme_DeepSea,
        Theme_Max
    };
    static int g_currentTheme;
    static ImVec4 g_colorBgMain;
    static ImVec4 g_colorBgPanel;
    static ImVec4 g_colorAccent;
    static ImVec4 g_colorAccentSoft;
    static ImVec4 g_colorAccentGlow;
    
    // Fonts
    static ImFont* g_fontDefault;
    static ImFont* g_fontH1;
    static ImFont* g_fontH2;
    static ImFont* g_fontH3;

    struct LoadedFont {
        std::string name;
        std::string filePath;
        ImFont* fontPtr;
    };
    static std::vector<LoadedFont> g_loadedFonts;
    static void RenderFontSelect(const char* label, std::string& currentFontName);
    static ImFont* GetFontByName(const std::string& fontName);


    // Tab textures
    static void* g_tabTextures[8];
    static void* g_likeTexture;
    static void* g_downloadTexture;
    static bool InitializeTextures();
    static void ShutdownTextures();
    static void* LoadTextureFromResource(int resourceId);

    // Config Market
    struct MarketConfig {
        int id;
        std::string title;
        std::string description;
        std::string author;
        int likes;
        int downloads;
        std::string downloadUrl;
        int status; // 0 = idle, 1 = downloading, 2 = downloaded, 3 = error
    };
    static std::vector<MarketConfig> g_marketConfigs;
    static bool g_fetchingMarket;
    static bool g_marketFetchDone;
    static bool g_marketFetchFailed;
    static void FetchMarketConfigs();
    static void DownloadConfig(int index);
    static void RenderConfigMarket();
    
    // Sidebar active indicator tracking
    static float g_sidebarIndicatorY;
    static float g_sidebarTargetIndicatorY;
    
    // Particle plexus background system
    struct Particle {
        ImVec2 pos;
        ImVec2 vel;
        float size;
        float alpha;
        float speedScale;
    };
    static std::vector<Particle> g_particles;
    static void InitializeParticles();
    static void RenderParticles(ImDrawList* draw, ImVec2 pos, ImVec2 size, float alpha);
    
    // Style and theme
    static void ApplyTheme();
    static void ApplyThemePreset(int presetId);
    static void LoadFont();
    
    // Menu animation update
    static void UpdateAnimation(ULONGLONG now, float dt);
    
    // Menu rendering
    static void RenderMenu(float screenWidth, float screenHeight);
    
    // UI Helpers
    static bool RenderSidebarButton(const char* label, int index);
    static void RenderCustomSwitch(const char* label, bool* value);
    static void RenderSectionHeader(const char* label);
    static bool BeginSection(const char* label, bool* open);
    static void EndSection();
    
    // Cascading Modules
    static bool BeginModuleSettings(const char* label, bool* open);
    static void EndModuleSettings();
    
    // Dashboard / Info
    static void RenderDashboard();
    static void RenderSocialButtons();
    
    // Notification
    static void RenderNotification(float screenWidth, float screenHeight);
    
    // Low-level Render Helpers
    static void DrawShadow(ImDrawList* draw, ImVec2 pos, ImVec2 size, float rounding, float thickness, float opacity);
    static void AddTextGlow(ImDrawList* draw, ImFont* font, float fontSize, ImVec2 pos, ImU32 col, const char* text, float thickness = 3.0f);
};
