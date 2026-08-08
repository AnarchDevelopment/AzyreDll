/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <windows.h>

/// @brief AntiAFK module - Simulates WASD key presses periodically to avoid AFK disconnects
class AntiAFK {
public:
    static bool  g_enabled;
    static float g_intervalSecs;     // intervalo entre simulaciones (segundos)
    static float g_pressDurationMs;  // duracion de cada pulsacion (milisegundos)
    static bool  g_randomizeKeys;    // aleatorizar teclas WASD
    static bool  g_jump;             // incluir salto (Espacio)

    static ULONGLONG g_enableTime;
    static ULONGLONG g_disableTime;

    static void Initialize();
    static void Tick();
    static void RenderArrayList(struct ImDrawList* draw, struct ImVec2 arrayListStart, float& yPos, struct ImVec2& arrayListEnd);
    static void RenderMenu();
};
