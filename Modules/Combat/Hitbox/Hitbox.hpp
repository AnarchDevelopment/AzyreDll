/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <cstdint>
#include <windows.h>

// Forward declarations
class ImDrawList;
struct ImVec2;

// Hitbox Module
class Hitbox {
public:
    static bool g_hitboxEnabled;
    static float g_hitboxValue;
    static uintptr_t g_hitboxAddr;
    static void* g_hitboxCave;
    static BYTE g_hitboxBackup[8];
    static ULONGLONG g_hitboxEnableTime;
    static ULONGLONG g_hitboxDisableTime;

    // Initialize hitbox module
    static void Initialize(uintptr_t gameBase);

    // Enable/Disable hitbox
    static void Enable();
    static void Disable();

    // Render hitbox in array list
    static void RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd);

    // Render hitbox UI in menu
    static void RenderMenu();

    // Check if hitbox is enabled
    static bool IsEnabled() { return g_hitboxEnabled; }
};
