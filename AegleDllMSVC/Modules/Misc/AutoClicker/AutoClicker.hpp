/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <windows.h>

/// @brief AutoClicker module - Automatically clicks left/right mouse button at configurable CPS
class AutoClicker {
public:
    static bool  g_enabled;
    static bool  g_rightClick;       // click derecho en lugar de izquierdo
    static float g_cps;              // clicks per second
    static float g_randomRange;      // variacion aleatoria de CPS (+/-)
    static bool  g_holdMode;         // solo hace click mientras se mantiene presionado el boton

    static ULONGLONG g_enableTime;
    static ULONGLONG g_disableTime;

    static void Initialize();
    static void Tick();              // llamar cada frame desde dllmain
    static void RenderArrayList(struct ImDrawList* draw, struct ImVec2 arrayListStart, float& yPos, struct ImVec2& arrayListEnd);
    static void RenderMenu();
};
