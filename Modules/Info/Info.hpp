/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include "ImGui/imgui.h"
#include <windows.h>
#include <string>
#include <cstdint>

/// @brief Info module - Displays information about the menu, repository, and logo
class Info {
public:
    /// @brief Initialize the Info module and load the logo texture
    static void Initialize();
    
    /// @brief Render the Info menu tab
    static void RenderMenu();
    
    /// @brief Cleanup resources
    static void Shutdown();

    /// @brief Play click sound
    static void PlayClickSound();

    static ImTextureID g_logoTexture;
    static int g_logoWidth;
    static int g_logoHeight;

    // GitHub release info
    static std::string g_releaseBody;   // Markdown body of the latest release
    static bool g_fetchInProgress;      // true while background thread is running
    static bool g_fetchDone;            // true once fetch has completed (success or fail)
    static bool g_fetchFailed;          // true if fetch resulted in an error
    static bool g_showReleaseModal;     // controls the popup window

    /// @brief Kick off a WinHTTP background fetch of the latest GitHub release body
    static void FetchLatestRelease();

private:
    
    // Audio resources
    static uint8_t* g_audioData;
    static uint32_t g_audioDataSize;
    
    /// @brief Load logo image from RC resource
    static bool LoadLogoFromResource();
    
    /// @brief Load image data into ImGui texture
    static ImTextureID LoadTextureFromMemory(const unsigned char* data, int size);
    
    /// @brief Load audio from RC resource
    static bool LoadAudioFromResource();
    
    /// @brief Initialize sound from loaded audio data (memory only, no files)
    static void InitSoundFromMemory();
};
