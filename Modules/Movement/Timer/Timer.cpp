/*
Under an4rch Development Public Source License 1.0
*/

#include "Timer.hpp"
#include "Animations/Animations.hpp"
#include "ImGui/imgui.h"
#include "GUI/GUI.hpp"
#include <windows.h>
#include <cstdio>
#include <cmath>

// Static member initialization
bool Timer::g_timerEnabled = false;
float Timer::g_timerValue = 1.0f;
uintptr_t Timer::g_timerAddr = 0;
HANDLE Timer::g_timerWriteThread = NULL;
bool Timer::g_timerRunning = false;
ULONGLONG Timer::g_timerEnableTime = 0;
ULONGLONG Timer::g_timerDisableTime = 0;

// Forward declarations for helper functions

void Timer::Initialize(uintptr_t gameBase) {
    g_timerAddr = gameBase + 0xB52AA8;  // Timer multiplicador offset
}

DWORD WINAPI Timer::TimerWriteThread(LPVOID lpParam) {
    if (!g_timerAddr) return 0;
    
    DWORD oldProtect = 0;
    // Cambiar permisos una sola vez al inicio
    BOOL protectChanged = VirtualProtect((void*)g_timerAddr, sizeof(double), PAGE_EXECUTE_READWRITE, &oldProtect);
    
    while (g_timerRunning) {
        if (g_timerAddr && protectChanged) {
            double memValue = (double)Timer::g_timerValue * 1000.0;
            // Escribir directamente
            *(double*)g_timerAddr = memValue;
        }
        Sleep(10);  // 100Hz
    }
    
    // Restaurar permisos si fue cambiado
    if (protectChanged) {
        VirtualProtect((void*)g_timerAddr, sizeof(double), oldProtect, &oldProtect);
    }
    return 0;
}

void Timer::Disable() {
    if (g_timerWriteThread) {
        g_timerDisableTime = GetTickCount64();
        g_timerRunning = false;
        WaitForSingleObject(g_timerWriteThread, 2000);
        CloseHandle(g_timerWriteThread);
        g_timerWriteThread = NULL;
    }
}

void Timer::Enable() {
    if (g_timerWriteThread) Disable();
    
    g_timerEnableTime = GetTickCount64();
    g_timerRunning = true;
    g_timerWriteThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)TimerWriteThread, NULL, 0, NULL);
}

void Timer::RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd) {
    const float FADE_OUT_TIME = 0.15f;
    const float FADE_IN_TIME = 0.12f;
    const float SLIDE_TIME = 0.25f;

    // Timer module
    if (g_timerEnabled || g_timerDisableTime > 0) {
        float timeSinceEnable = (float)(GetTickCount64() - g_timerEnableTime) / 1000.0f;
        float timeSinceDisable = (float)(GetTickCount64() - g_timerDisableTime) / 1000.0f;
        
        float timerAlpha = 255.0f;
        float slideOffset = 0.0f;
        
        if (g_timerEnabled) {
            timerAlpha = Animations::SmoothInertia(fminf(1.0f, timeSinceEnable / FADE_IN_TIME)) * 255.0f;
            // Slide in animation
            float slideProgress = fminf(1.0f, timeSinceEnable / SLIDE_TIME);
            slideOffset = Animations::SmoothInertia(slideProgress) * 60.0f - 60.0f;
        } else if (timeSinceDisable < FADE_OUT_TIME) {
            timerAlpha = Animations::SmoothInertia(1.0f - (timeSinceDisable / FADE_OUT_TIME)) * 255.0f;
        } else {
            g_timerDisableTime = 0;
        }
        
        if (timerAlpha > 1.0f) {
            char tBuf[64];
            sprintf_s(tBuf, "Timer - %.1fx", g_timerValue);
            float wT = ImGui::CalcTextSize(tBuf).x;
            float xPosT = arrayListStart.x + 290.0f - wT - 10;
            draw->AddText(ImVec2(xPosT + slideOffset - 1, yPos + 1), IM_COL32(0, 0, 0, 220), tBuf); // Sombra
            draw->AddText(ImVec2(xPosT + slideOffset, yPos), IM_COL32(100, 200, 255, (int)timerAlpha), tBuf);
            yPos += 18.0f;
            arrayListEnd.y = yPos;
        }
    }
}

void Timer::RenderMenu() {
    bool prev = g_timerEnabled;
    GUI::RenderCustomSwitch("Timer", &g_timerEnabled);
    if (prev != g_timerEnabled) {
        if (g_timerEnabled) Enable(); else Disable();
    }
    
    if (GUI::BeginModuleSettings("Timer", &g_timerEnabled)) {
        if (ImGui::SliderFloat("##timerSlider", &g_timerValue, 0.1f, 20.0f, "Multiplicador: %.1fx")) {
            // Value updates automatically
        }
        ImGui::TextDisabled("Value: %.0f", (double)g_timerValue * 1000.0);
        GUI::EndModuleSettings();
    }
}
