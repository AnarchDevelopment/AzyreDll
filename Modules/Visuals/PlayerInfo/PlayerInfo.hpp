/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <windows.h>
#include <string>
#include "../../../ImGui/imgui.h"

// Forward declarations
struct ImDrawList;
struct ImVec2;
struct HudElement;

/// @brief Player Info module - Displays a box with the player's skin head and nickname
class PlayerInfo {
public:
    // Static member variables
    static bool g_showPlayerInfo;
    static ULONGLONG g_enableTime;
    static ULONGLONG g_disableTime;
    static float g_anim;
    static HudElement* g_playerInfoHud;

    // Player data
    static std::string g_playerName;
    static void* g_skinTexture;
    static void* g_skinFaceTex;
    static void* g_skinHatTex;
    static int g_texWidth;
    static int g_texHeight;

    // Settings
    static bool g_showHatLayer;
    static float g_headSize;
    static float g_textScale;
    static bool g_showBackground;
    static float g_bgOpacity;
    static float g_bgRadius;
    static bool g_showBorder;
    static ImVec4 g_borderColor;
    static ImVec4 g_nameColor;
    static bool g_headRounded;
    static float g_headRadius;

    /// @brief Initialize Player Info with HudElement reference
    static void Initialize(HudElement* hud);

    /// @brief Update animation state (call from main render loop)
    static void UpdateAnimation(ULONGLONG now);

    /// @brief Render player info display
    static void RenderDisplay();

    /// @brief Render menu controls
    static void RenderMenu();

    /// @brief Release resources
    static void Shutdown();

    /// @brief Re-read nickname (options.txt) and skin (custom.png)
    static void RefreshPlayerData();

private:
    /// @brief Load custom.png from the Minecraft data folder into a texture
    static bool LoadSkinTexture();

    /// @brief Resolve the Minecraft data folder (options.txt / custom.png live here)
    static std::string GetMinecraftDataDir();

    /// @brief Parse mp_username from options.txt
    static std::string ReadPlayerName();
};
