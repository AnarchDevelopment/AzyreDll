/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <cstdint>
#include <windows.h>

// Forward declarations
class ImDrawList;
struct ImVec2;

// Timer Module
class Timer {
public:
    static bool g_timerEnabled;
    static float g_timerValue;
    static uintptr_t g_timerAddr;
    static HANDLE g_timerWriteThread;
    static bool g_timerRunning;
    static ULONGLONG g_timerEnableTime;
    static ULONGLONG g_timerDisableTime;

    // Initialize timer module
    static void Initialize(uintptr_t gameBase);

    // Enable/Disable timer
    static void Enable();
    static void Disable();

    // Timer write thread
    static DWORD WINAPI TimerWriteThread(LPVOID lpParam);

    // Render timer in array list
    static void RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd);

    // Render timer UI in menu
    static void RenderMenu();

    // Check if timer is enabled
    static bool IsEnabled() { return g_timerEnabled; }
};
