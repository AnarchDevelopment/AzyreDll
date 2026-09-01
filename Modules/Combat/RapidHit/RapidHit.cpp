/*
Under an4rch Development Public Source License 1.0
*/

#include "RapidHit.hpp"
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
bool RapidHit::g_rapidHitEnabled = false;
uintptr_t RapidHit::g_rapidHitAddr = 0;
void* RapidHit::g_rapidHitCave = nullptr;
BYTE RapidHit::g_rapidHitBackup[8] = { 0 };
ULONGLONG RapidHit::g_rapidHitEnableTime = 0;
ULONGLONG RapidHit::g_rapidHitDisableTime = 0;

void RapidHit::Initialize(uintptr_t gameBase) {
    g_rapidHitEnableTime = GetTickCount64();
}

void RapidHit::ScanPattern(uintptr_t gameBase, size_t imageSize) {
    // Pattern from Cheat Engine: 0F B6 4F 78 8B 44 24 60 84 C9 0F 84 ** ** ** ** 85 C0
    // We search for the unique prefix: 0F B6 4F 78 8B 44 24 60 84 C9 0F 84
    BYTE pattern[] = { 0x0F, 0xB6, 0x4F, 0x78, 0x8B, 0x44, 0x24, 0x60, 0x84, 0xC9, 0x0F, 0x84 };
    g_rapidHitAddr = PatternScan::Scan(gameBase, imageSize, pattern, sizeof(pattern));
}

void RapidHit::Disable() {
    if (!g_rapidHitAddr || g_rapidHitBackup[0] == 0) return;
    g_rapidHitDisableTime = GetTickCount64();
    DWORD old;
    VirtualProtect((void*)g_rapidHitAddr, 8, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void*)g_rapidHitAddr, g_rapidHitBackup, 8);
    VirtualProtect((void*)g_rapidHitAddr, 8, old, &old);
}

void RapidHit::Enable() {
    if (!g_rapidHitAddr) return;
    g_rapidHitEnableTime = GetTickCount64();
    if (!g_rapidHitCave) g_rapidHitCave = AllocateNear::Allocate(g_rapidHitAddr, 1024);
    if (!g_rapidHitCave) return;

    memcpy(g_rapidHitBackup, (void*)g_rapidHitAddr, 8);

    // Build the code cave
    // Original:
    //   movzx ecx, byte ptr [rdi+78]   ; 0F B6 4F 78
    //   mov eax, [rsp+60]              ; 8B 44 24 60
    //
    // Modified (rapid hit):
    //   movzx ecx, byte ptr [rdi+79]   ; 0F B6 4F 79  (changed 78 to 79)
    //   mov eax, [rsp+60]              ; 8B 44 24 60

    BYTE shellcode[64] = { 0 };
    int p = 0;

    // movzx ecx, byte ptr [rdi+79] - modified offset from 78 to 79
    shellcode[p++] = 0x0F;
    shellcode[p++] = 0xB6;
    shellcode[p++] = 0x4F;
    shellcode[p++] = 0x79;  // Changed from 0x78 to 0x79

    // mov eax, [rsp+60] - original
    shellcode[p++] = 0x8B;
    shellcode[p++] = 0x44;
    shellcode[p++] = 0x24;
    shellcode[p++] = 0x60;

    // jmp back to return address (after the hooked 8 bytes)
    shellcode[p++] = 0xE9;
    uintptr_t retAddr = g_rapidHitAddr + 8;
    int32_t jmpBack = (int32_t)(retAddr - ((uintptr_t)g_rapidHitCave + p + 4));
    memcpy(&shellcode[p], &jmpBack, 4);
    p += 4;

    memcpy(g_rapidHitCave, shellcode, p);

    DWORD old;
    VirtualProtect((void*)g_rapidHitAddr, 8, PAGE_EXECUTE_READWRITE, &old);

    // Build the JMP patch (8 bytes: jmp + 3 nops)
    BYTE patch[8] = { 0 };
    // jmp to cave
    patch[0] = 0xE9;
    int32_t jmpToCave = (int32_t)((uintptr_t)g_rapidHitCave - (g_rapidHitAddr + 5));
    memcpy(&patch[1], &jmpToCave, 4);
    // nop remaining 3 bytes
    patch[5] = 0x90;
    patch[6] = 0x90;
    patch[7] = 0x90;

    memcpy((void*)g_rapidHitAddr, patch, 8);
    VirtualProtect((void*)g_rapidHitAddr, 8, old, &old);
}

void RapidHit::RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd) {
    const float FADE_OUT_TIME = 0.15f;
    const float FADE_IN_TIME = 0.12f;
    const float SLIDE_TIME = 0.25f;

    if (g_rapidHitEnabled || g_rapidHitDisableTime > 0) {
        float timeSinceEnable = (float)(GetTickCount64() - g_rapidHitEnableTime) / 1000.0f;
        float timeSinceDisable = (float)(GetTickCount64() - g_rapidHitDisableTime) / 1000.0f;
        
        float rapidHitAlpha = 255.0f;
        float slideOffset = 0.0f;
        
        if (g_rapidHitEnabled) {
            rapidHitAlpha = Animations::SmoothInertia(fminf(1.0f, timeSinceEnable / FADE_IN_TIME)) * 255.0f;
            float slideProgress = fminf(1.0f, timeSinceEnable / SLIDE_TIME);
            slideOffset = Animations::SmoothInertia(slideProgress) * 60.0f - 60.0f;
        } else if (timeSinceDisable < FADE_OUT_TIME) {
            rapidHitAlpha = Animations::SmoothInertia(1.0f - (timeSinceDisable / FADE_OUT_TIME)) * 255.0f;
        } else {
            g_rapidHitDisableTime = 0;
        }
        
        if (rapidHitAlpha > 1.0f) {
            char rhBuf[64];
            sprintf_s(rhBuf, "Rapid Hit");
            float wRH = ImGui::CalcTextSize(rhBuf).x;
            float xPosRH = arrayListStart.x + 290.0f - wRH - 10;
            draw->AddText(ImVec2(xPosRH + slideOffset - 1, yPos + 1), IM_COL32(0, 0, 0, 220), rhBuf);
            draw->AddText(ImVec2(xPosRH + slideOffset, yPos), IM_COL32(255, 100, 50, (int)rapidHitAlpha), rhBuf);
            yPos += 18.0f;
            arrayListEnd.y = yPos;
        }
    }
}

void RapidHit::RenderMenu() {
    bool prev = g_rapidHitEnabled;
    GUI::RenderCustomSwitch("Rapid Hit", &g_rapidHitEnabled);
    if (prev != g_rapidHitEnabled) {
        if (g_rapidHitEnabled) Enable(); else Disable();
    }
}
