/*
Under an4rch Development Public Source License 1.0
*/

#include "FullBright.hpp"
#include "../../../Animations/Animations.hpp"
#include "../../Alloc/AllocateNear.hpp"
#include "../../PatternScan/PatternScan.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../GUI/GUI.hpp"
#include <windows.h>
#include <cstring>
#include <cstdio>
#include <cmath>

// Static member initialization
bool FullBright::g_fullBrightEnabled = false;
float FullBright::g_fullBrightValue = 100.0f;
uintptr_t FullBright::g_fullBrightAddr = 0;
void* FullBright::g_fullBrightCave = nullptr;
BYTE FullBright::g_fullBrightBackup[8] = { 0 };
ULONGLONG FullBright::g_fullBrightEnableTime = 0;
ULONGLONG FullBright::g_fullBrightDisableTime = 0;

void FullBright::ScanPattern(uintptr_t gameBase, size_t imageSize) {
    if (g_fullBrightAddr) return;
    BYTE pattern[] = {0xF3, 0x0F, 0x10, 0x80, 0xA0, 0x01, 0x00, 0x00};
    g_fullBrightAddr = PatternScan::Scan(gameBase, imageSize, pattern, sizeof(pattern));
}

void FullBright::Initialize(uintptr_t gameBase) {
    // Pattern scanning for FullBright is done via ScanPattern (called from Module::Initialize)
    (void)gameBase;
}

void FullBright::Disable() {
    if (!g_fullBrightAddr || g_fullBrightBackup[0] == 0) return;
    g_fullBrightDisableTime = GetTickCount64();
    DWORD old;
    VirtualProtect((void*)g_fullBrightAddr, 8, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void*)g_fullBrightAddr, g_fullBrightBackup, 8);
    VirtualProtect((void*)g_fullBrightAddr, 8, old, &old);
}

void FullBright::Enable() {
    if (!g_fullBrightAddr) return;
    g_fullBrightEnableTime = GetTickCount64();
    if (!g_fullBrightCave) g_fullBrightCave = AllocateNear::Allocate(g_fullBrightAddr, 1024);
    if (!g_fullBrightCave) return;

    memcpy(g_fullBrightBackup, (void*)g_fullBrightAddr, 8);

    BYTE shellcode[64] = { 0 };
    int p = 0;
    // mov eax, <bits de g_fullBrightValue>
    shellcode[p++] = 0xB8; memcpy(&shellcode[p], &g_fullBrightValue, 4); p += 4;
    // movd xmm0, eax — copia los bits tal cual, preservando el float (cvtsi2ss lo convertía como entero)
    shellcode[p++] = 0x66; shellcode[p++] = 0x0F; shellcode[p++] = 0x6E; shellcode[p++] = 0xC0;
    // jmp back
    shellcode[p++] = 0xE9;
    uintptr_t retAddr = g_fullBrightAddr + 8;
    int32_t jmpBack = (int32_t)(retAddr - ((uintptr_t)g_fullBrightCave + p + 4));
    memcpy(&shellcode[p], &jmpBack, 4); p += 4;

    memcpy(g_fullBrightCave, shellcode, p);

    DWORD old;
    VirtualProtect((void*)g_fullBrightAddr, 8, PAGE_EXECUTE_READWRITE, &old);
    BYTE patch[8] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90 };
    int32_t jmpToCave = (int32_t)((uintptr_t)g_fullBrightCave - (g_fullBrightAddr + 5));
    memcpy(&patch[1], &jmpToCave, 4);
    memcpy((void*)g_fullBrightAddr, patch, 8);
    VirtualProtect((void*)g_fullBrightAddr, 8, old, &old);
}

void FullBright::RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd) {
    const float FADE_OUT_TIME = 0.15f;
    const float FADE_IN_TIME = 0.12f;
    const float SLIDE_TIME = 0.25f;

    // FullBright module
    if (g_fullBrightEnabled || g_fullBrightDisableTime > 0) {
        float timeSinceEnable = (float)(GetTickCount64() - g_fullBrightEnableTime) / 1000.0f;
        float timeSinceDisable = (float)(GetTickCount64() - g_fullBrightDisableTime) / 1000.0f;
        
        float fullBrightAlpha = 255.0f;
        float slideOffset = 0.0f;
        
        if (g_fullBrightEnabled) {
            fullBrightAlpha = Animations::SmoothInertia(fminf(1.0f, timeSinceEnable / FADE_IN_TIME)) * 255.0f;
            // Slide in animation
            float slideProgress = fminf(1.0f, timeSinceEnable / SLIDE_TIME);
            slideOffset = Animations::SmoothInertia(slideProgress) * 60.0f - 60.0f;
        } else if (timeSinceDisable < FADE_OUT_TIME) {
            fullBrightAlpha = Animations::SmoothInertia(1.0f - (timeSinceDisable / FADE_OUT_TIME)) * 255.0f;
        } else {
            g_fullBrightDisableTime = 0;
        }
        
        if (fullBrightAlpha > 1.0f) {
            char fBuf[64];
            sprintf_s(fBuf, "FullBright");
            float wF = ImGui::CalcTextSize(fBuf).x;
            float xPosF = arrayListStart.x + 290.0f - wF - 10;
            draw->AddText(ImVec2(xPosF + slideOffset - 1, yPos + 1), IM_COL32(0, 0, 0, 220), fBuf); // Sombra
            draw->AddText(ImVec2(xPosF + slideOffset, yPos), IM_COL32(255, 255, 100, (int)fullBrightAlpha), fBuf);
            yPos += 18.0f;
            arrayListEnd.y = yPos;
        }
    }
}

void FullBright::RenderMenu() {
    // FullBright Checkbox
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Unstable Feature - May Cause Performance Issues\nFullBright is a little buggy");
    bool prev = g_fullBrightEnabled;
    GUI::RenderCustomSwitch("FullBright", &g_fullBrightEnabled);
    if (prev != g_fullBrightEnabled) {
        if (g_fullBrightEnabled) Enable(); else Disable();
    }
}
