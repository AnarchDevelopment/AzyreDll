/*
Under an4rch Development Public Source License 1.0
*/

#include "Reach.hpp"
#include "../../../Animations/Animations.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../GUI/GUI.hpp"
#include <windows.h>
#include <cstdio>
#include <cmath>

// Static member initialization
float* Reach::g_reachPtr = nullptr;
float Reach::g_reachValue = 3.0f;
bool Reach::g_reachEnabled = false;
ULONGLONG Reach::g_reachEnableTime = 0;
ULONGLONG Reach::g_reachDisableTime = 0;

void Reach::Initialize(uintptr_t gameBase) {
    g_reachPtr = (float*)(gameBase + 0xB52A70);
    g_reachEnableTime = GetTickCount64();
    if (g_reachEnabled) {
        UpdateValue(g_reachValue);
    }
}

void Reach::UpdateValue(float newValue) {
    if (!g_reachPtr) return;
    
    g_reachValue = newValue;
    DWORD old;
    VirtualProtect(g_reachPtr, 4, PAGE_EXECUTE_READWRITE, &old);
    *g_reachPtr = newValue;
    VirtualProtect(g_reachPtr, 4, old, &old);
}

void Reach::SetEnabled(bool enabled) {
    if (g_reachEnabled == enabled) {
        return;
    }

    g_reachEnabled = enabled;
    if (g_reachEnabled) {
        g_reachEnableTime = GetTickCount64();
        g_reachDisableTime = 0;
        UpdateValue(g_reachValue);
    } else {
        g_reachDisableTime = GetTickCount64();
        UpdateValue(3.0f);
    }
}

void Reach::RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd) {
    const float FADE_OUT_TIME = 0.15f;
    const float FADE_IN_TIME = 0.12f;
    const float SLIDE_TIME = 0.25f;

    if (g_reachEnabled || g_reachDisableTime > 0) {
        float timeSinceEnable = (float)(GetTickCount64() - g_reachEnableTime) / 1000.0f;
        float timeSinceDisable = (float)(GetTickCount64() - g_reachDisableTime) / 1000.0f;

        float reachAlpha = 255.0f;
        float slideOffset = 0.0f;

        if (g_reachEnabled) {
            reachAlpha = Animations::SmoothInertia(fminf(1.0f, timeSinceEnable / FADE_IN_TIME)) * 255.0f;
            float slideProgress = fminf(1.0f, timeSinceEnable / SLIDE_TIME);
            slideOffset = Animations::SmoothInertia(slideProgress) * 60.0f - 60.0f;
        } else if (timeSinceDisable < FADE_OUT_TIME) {
            reachAlpha = Animations::SmoothInertia(1.0f - (timeSinceDisable / FADE_OUT_TIME)) * 255.0f;
        } else {
            g_reachDisableTime = 0;
        }

        if (reachAlpha > 1.0f) {
            char rBuf[64];
            sprintf_s(rBuf, "Reach - %.2f", g_reachValue);
            float wR = ImGui::CalcTextSize(rBuf).x;
            float xPos = arrayListStart.x + 290.0f - wR - 10;

            draw->AddText(ImVec2(xPos + slideOffset - 1, yPos + 1), IM_COL32(0, 0, 0, 220), rBuf);
            draw->AddText(ImVec2(xPos + slideOffset, yPos), IM_COL32(0, 200, 255, (int)reachAlpha), rBuf);
            yPos += 18.0f;
            arrayListEnd.y = yPos;
        }
    }
}

void Reach::RenderMenu() {
    bool prev = g_reachEnabled;
    GUI::RenderCustomSwitch("Enable Reach", &g_reachEnabled);
    if (prev != g_reachEnabled) {
        SetEnabled(g_reachEnabled);
    }

    if (GUI::BeginModuleSettings("Reach", &g_reachEnabled)) {
        if (ImGui::SliderFloat("Reach Distance", &g_reachValue, 3.0f, 15.0f, "%.2f")) {
            UpdateValue(g_reachValue);
        }
        GUI::EndModuleSettings();
    }
}
