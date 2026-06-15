/*
Under an4rch Development Public Source License 1.0
*/

#include "AutoSprint.hpp"
#include "../../Alloc/AllocateNear.hpp"
#include "../../../Animations/Animations.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../GUI/GUI.hpp"
#include <windows.h>
#include <cstring>
#include <cstdio>
#include <cmath>

// Static member initialization
bool AutoSprint::g_autoSprintEnabled = false;
uintptr_t AutoSprint::g_autoSprintAddr = 0;
void* AutoSprint::g_autoSprintCave = nullptr;
BYTE AutoSprint::g_autoSprintBackup[11] = { 0 };
ULONGLONG AutoSprint::g_autoSprintEnableTime = 0;
ULONGLONG AutoSprint::g_autoSprintDisableTime = 0;

// Pattern scanner - finds byte patterns in memory
uintptr_t AutoSprint::PatternScan(uintptr_t start, size_t size, const BYTE* pattern, size_t patternSize) {
    for (size_t i = 0; i <= size - patternSize; ++i) {
        if (memcmp((void*)(start + i), pattern, patternSize) == 0) {
            return start + i;
        }
    }
    return 0;
}

void AutoSprint::Initialize(uintptr_t gameBase) {
    // Pattern scanning for AutoSprint is done in dllmain
}

void AutoSprint::Disable() {
    if (!g_autoSprintAddr || g_autoSprintBackup[0] == 0) return;
    g_autoSprintDisableTime = GetTickCount64();
    DWORD old;
    VirtualProtect((void*)g_autoSprintAddr, 11, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void*)g_autoSprintAddr, g_autoSprintBackup, 11);
    VirtualProtect((void*)g_autoSprintAddr, 11, old, &old);
}

void AutoSprint::Enable() {
    if (!g_autoSprintAddr) return;
    g_autoSprintEnableTime = GetTickCount64();
    if (!g_autoSprintCave) g_autoSprintCave = AllocateNear::Allocate(g_autoSprintAddr, 1024);
    if (!g_autoSprintCave) return;

    memcpy(g_autoSprintBackup, (void*)g_autoSprintAddr, 11);

    BYTE shellcode[64] = { 0 };
    int p = 0;
    // mov eax, 6
    shellcode[p++] = 0xB8; int sprintValue = 6; memcpy(&shellcode[p], &sprintValue, 4); p += 4;
    // jmp back
    shellcode[p++] = 0xE9;
    uintptr_t retAddr = g_autoSprintAddr + 11;
    uintptr_t cur = (uintptr_t)g_autoSprintCave + p;
    int32_t rel = (int32_t)(retAddr - (cur + 4));
    memcpy(&shellcode[p], &rel, 4); p += 4;

    memcpy(g_autoSprintCave, shellcode, p);

    DWORD old;
    VirtualProtect((void*)g_autoSprintAddr, 11, PAGE_EXECUTE_READWRITE, &old);
    BYTE patch[11] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };
    int32_t jmpToCave = (int32_t)((uintptr_t)g_autoSprintCave - (g_autoSprintAddr + 5));
    memcpy(&patch[1], &jmpToCave, 4);
    memcpy((void*)g_autoSprintAddr, patch, 11);
    VirtualProtect((void*)g_autoSprintAddr, 11, old, &old);
}

void AutoSprint::RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd) {
    const float FADE_OUT_TIME = 0.15f;
    const float FADE_IN_TIME = 0.12f;
    const float SLIDE_TIME = 0.25f;

    // AutoSprint module
    if (g_autoSprintEnabled || g_autoSprintDisableTime > 0) {
        float timeSinceEnable = (float)(GetTickCount64() - g_autoSprintEnableTime) / 1000.0f;
        float timeSinceDisable = (float)(GetTickCount64() - g_autoSprintDisableTime) / 1000.0f;
        
        float autoSprintAlpha = 255.0f;
        float slideOffset = 0.0f;
        
        if (g_autoSprintEnabled) {
            autoSprintAlpha = Animations::SmoothInertia(fminf(1.0f, timeSinceEnable / FADE_IN_TIME)) * 255.0f;
            // Slide in animation
            float slideProgress = fminf(1.0f, timeSinceEnable / SLIDE_TIME);
            slideOffset = Animations::SmoothInertia(slideProgress) * 60.0f - 60.0f;
        } else if (timeSinceDisable < FADE_OUT_TIME) {
            autoSprintAlpha = Animations::SmoothInertia(1.0f - (timeSinceDisable / FADE_OUT_TIME)) * 255.0f;
        } else {
            g_autoSprintDisableTime = 0;
        }
        
        if (autoSprintAlpha > 1.0f) {
            char aBuf[64];
            sprintf_s(aBuf, "AutoSprint");
            float wA = ImGui::CalcTextSize(aBuf).x;
            float xPosA = arrayListStart.x + 290.0f - wA - 10;
            draw->AddText(ImVec2(xPosA + slideOffset - 1, yPos + 1), IM_COL32(0, 0, 0, 220), aBuf); // Sombra
            draw->AddText(ImVec2(xPosA + slideOffset, yPos), IM_COL32(60, 255, 60, (int)autoSprintAlpha), aBuf);
            yPos += 18.0f;
            arrayListEnd.y = yPos;
        }
    }
}

void AutoSprint::RenderMenu() {
    bool prev = g_autoSprintEnabled;
    GUI::RenderCustomSwitch("AutoSprint", &g_autoSprintEnabled);
    if (prev != g_autoSprintEnabled) {
        if (g_autoSprintEnabled) Enable(); else Disable();
    }
}
