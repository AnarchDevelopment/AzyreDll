/*
Under an4rch Development Public Source License 1.0
*/

#include "UnlockFPS.hpp"
#include "Animations/Animations.hpp"
#include "ImGui/imgui.h"
#include "GUI/GUI.hpp"
#include <Windows.h>
#include <cstdio>
#include <cmath>

// Static member initialization
bool UnlockFPS::g_unlockFpsEnabled = false;
float UnlockFPS::g_fpsLimit = 60.0f;
ULONGLONG UnlockFPS::g_unlockFpsEnableTime = 0;
ULONGLONG UnlockFPS::g_unlockFpsDisableTime = 0;

// Internal state
static float targetFPS = 60.0f;
static LARGE_INTEGER perfFrequency = { 0 };
static LONGLONG frameDurationTicks = 0;
static LARGE_INTEGER lastPresentTime = { 0 };
static bool isFirstFrame = true;
static bool wasEnabled = false;
static float lastSetFPS = 60.0f;

// Constants for animation
#define FADE_IN_TIME 0.3f
#define FADE_OUT_TIME 0.3f
#define SLIDE_TIME 0.4f

void UnlockFPS::Initialize() {
    if (perfFrequency.QuadPart == 0) {
        QueryPerformanceFrequency(&perfFrequency);
        if (perfFrequency.QuadPart == 0) {
            perfFrequency.QuadPart = 1;
        }
        frameDurationTicks = (LONGLONG)((double)perfFrequency.QuadPart / targetFPS);
    }
}

void UnlockFPS::UpdateFPS() {
    if (!g_unlockFpsEnabled) {
        isFirstFrame = true;
        wasEnabled = false;
        lastPresentTime.QuadPart = 0;
        return;
    }

    if (perfFrequency.QuadPart == 0) {
        Initialize();
    }

    if (!wasEnabled || lastSetFPS != g_fpsLimit) {
        SetFPS(g_fpsLimit);
        lastSetFPS = g_fpsLimit;
        wasEnabled = true;
    }

    if (frameDurationTicks <= 0) {
        return;
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    if (isFirstFrame || lastPresentTime.QuadPart == 0) {
        lastPresentTime = now;
        isFirstFrame = false;
        return;
    }

    LONGLONG elapsed = now.QuadPart - lastPresentTime.QuadPart;
    if (elapsed < frameDurationTicks) {
        LONGLONG remaining = frameDurationTicks - elapsed;
        DWORD sleepMs = (DWORD)((remaining * 1000) / perfFrequency.QuadPart);
        if (sleepMs > 1) {
            Sleep(sleepMs - 1);
        }
        do {
            QueryPerformanceCounter(&now);
            elapsed = now.QuadPart - lastPresentTime.QuadPart;
        } while (elapsed < frameDurationTicks);
    }

    lastPresentTime = now;
}

void UnlockFPS::SetFPS(float fps)
{
    if (fps < 5.0f) fps = 5.0f;
    if (fps > 1000.0f) fps = 1000.0f;

    targetFPS = fps;
    if (perfFrequency.QuadPart == 0) {
        QueryPerformanceFrequency(&perfFrequency);
        if (perfFrequency.QuadPart == 0) {
            perfFrequency.QuadPart = 1;
        }
    }

    frameDurationTicks = (LONGLONG)((double)perfFrequency.QuadPart / targetFPS);
}

float UnlockFPS::GetFPS()
{
    return targetFPS;
}

void UnlockFPS::RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd) {
    if (g_unlockFpsEnabled || g_unlockFpsDisableTime > 0) {
        ULONGLONG now = GetTickCount64();
        float timeSinceEnable = (float)(now - g_unlockFpsEnableTime) / 1000.0f;
        float timeSinceDisable = (float)(now - g_unlockFpsDisableTime) / 1000.0f;

        float unlockFpsAlpha = 255.0f;
        float slideOffset = 0.0f;

        if (g_unlockFpsEnabled) {
            unlockFpsAlpha = Animations::SmoothInertia(fminf(1.0f, timeSinceEnable / FADE_IN_TIME)) * 255.0f;
            float slideProgress = fminf(1.0f, timeSinceEnable / SLIDE_TIME);
            slideOffset = Animations::SmoothInertia(slideProgress) * 60.0f - 60.0f;
        } else if (timeSinceDisable < FADE_OUT_TIME) {
            unlockFpsAlpha = Animations::SmoothInertia(1.0f - (timeSinceDisable / FADE_OUT_TIME)) * 255.0f;
        } else {
            g_unlockFpsDisableTime = 0;
        }

        if (unlockFpsAlpha > 1.0f && draw) {
            char uBuf[64];
            sprintf_s(uBuf, sizeof(uBuf), "Unlock FPS - %.0f", g_fpsLimit);
            ImVec2 textSize = ImGui::CalcTextSize(uBuf);
            float xPosU = arrayListStart.x + 300.0f - textSize.x - 10.0f;

            draw->AddText(ImVec2(xPosU + slideOffset - 1, yPos + 1), IM_COL32(0, 0, 0, 220), uBuf);
            draw->AddText(ImVec2(xPosU + slideOffset, yPos), IM_COL32(100, 255, 100, (int)unlockFpsAlpha), uBuf);
            yPos += 18.0f;
            arrayListEnd.y = yPos;
        }
    }
}

void UnlockFPS::RenderMenu() {
    bool prev = g_unlockFpsEnabled;
    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "This module is very unstable; use it at your own risk.\n It causes tearing and other visual problems.");
    GUI::RenderCustomSwitch("Unlock FPS", &g_unlockFpsEnabled);
    if (prev != g_unlockFpsEnabled) {
        if (g_unlockFpsEnabled) {
            g_unlockFpsEnableTime = GetTickCount64();
            g_unlockFpsDisableTime = 0;
        } else {
            g_unlockFpsDisableTime = GetTickCount64();
            g_unlockFpsEnableTime = 0;
        }
    }

    if (GUI::BeginModuleSettings("UnlockFPS", &g_unlockFpsEnabled)) {
        ImGui::SliderFloat("FPS Limit", &g_fpsLimit, 5.0f, 1000.0f, "%.0f FPS");
        GUI::EndModuleSettings();
    }
}
