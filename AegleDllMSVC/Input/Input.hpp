/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <windows.h>
#include "../ImGui/imgui.h"

/// @brief Input class - Manages all keyboard and mouse input handling
class Input {
public:
    // Keyboard state arrays
    static bool g_keys[256];
    static bool g_keysPressed[256];
    static bool g_keysReleased[256];
    
    // Mouse button state
    static bool g_prevLmbPressed;
    static bool g_prevRmbPressed;
    static bool g_lmbPressed;
    static bool g_rmbPressed;
    
    // Input methods
    static void UpdateKeyboard(bool menuOpen);
    static void UpdateMouse(HWND window, float screenWidth, float screenHeight, bool drawCursor);
    static void Update(HWND window, float screenWidth, float screenHeight, bool menuOpen, bool drawCursor);
    
    // Mouse button state queries
    static bool IsLMBPressed();
    static bool IsRMBPressed();
    static bool WasLMBPressed();
    static bool WasRMBPressed();
    
    // Input blocking for game input
    static HHOOK g_keyboardHook;
    static int KeyboardBlockHookProc(int nCode, WPARAM wParam, LPARAM lParam, bool menuOpen);
    static void BlockGameInput();
    static void UnblockGameInput();
    
    // Utility
    static ImGuiKey VKToImGuiKey(int vk);
};
