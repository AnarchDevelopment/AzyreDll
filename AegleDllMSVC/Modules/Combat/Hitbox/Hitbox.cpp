/*
Under an4rch Development Public Source License 1.0
*/

#include "Hitbox.hpp"
#include "../../../Animations/Animations.hpp"
#include "../../Alloc/AllocateNear.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../GUI/GUI.hpp"
#include <windows.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>

// Static member initialization
bool Hitbox::g_hitboxEnabled = false;
float Hitbox::g_hitboxValue = 0.6f;
uintptr_t Hitbox::g_hitboxAddr = 0;
void* Hitbox::g_hitboxCave = nullptr;
BYTE Hitbox::g_hitboxBackup[8] = { 0 };
ULONGLONG Hitbox::g_hitboxEnableTime = 0;
ULONGLONG Hitbox::g_hitboxDisableTime = 0;

void Hitbox::Initialize(uintptr_t gameBase) {
    g_hitboxAddr = gameBase + 0x4B57B0;
}

void Hitbox::Disable() {
    if (!g_hitboxAddr || g_hitboxBackup[0] == 0) return;
    g_hitboxDisableTime = GetTickCount64();
    DWORD old;
    VirtualProtect((void*)g_hitboxAddr, 8, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void*)g_hitboxAddr, g_hitboxBackup, 8);
    VirtualProtect((void*)g_hitboxAddr, 8, old, &old);
}

void Hitbox::Enable() {
    if (!g_hitboxAddr) return;
    g_hitboxEnableTime = GetTickCount64();
    if (!g_hitboxCave) g_hitboxCave = AllocateNear::Allocate(g_hitboxAddr, 1024);
    if (!g_hitboxCave) return;

    memcpy(g_hitboxBackup, (void*)g_hitboxAddr, 8);

    BYTE shellcode[64] = { 0 };
    int p = 0;
    // mov eax, [valor] | mov [rcx+D0], eax
    shellcode[p++] = 0xB8; memcpy(&shellcode[p], &g_hitboxValue, 4); p += 4; 
    shellcode[p++] = 0x89; shellcode[p++] = 0x81; shellcode[p++] = 0xD0; 
    shellcode[p++] = 0x00; shellcode[p++] = 0x00; shellcode[p++] = 0x00; 
    // Original bytes
    memcpy(&shellcode[p], g_hitboxBackup, 8); p += 8; 
    // jmp back
    shellcode[p++] = 0xE9; 
    uintptr_t retAddr = g_hitboxAddr + 8;
    int32_t jmpBack = (int32_t)(retAddr - ((uintptr_t)g_hitboxCave + p + 4));
    memcpy(&shellcode[p], &jmpBack, 4); p += 4;

    memcpy(g_hitboxCave, shellcode, p);

    DWORD old;
    VirtualProtect((void*)g_hitboxAddr, 8, PAGE_EXECUTE_READWRITE, &old);
    BYTE patch[8] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90 };
    int32_t jmpToCave = (int32_t)((uintptr_t)g_hitboxCave - (g_hitboxAddr + 5));
    memcpy(&patch[1], &jmpToCave, 4);
    memcpy((void*)g_hitboxAddr, patch, 8);
    VirtualProtect((void*)g_hitboxAddr, 8, old, &old);
}

void Hitbox::RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd) {
    const float FADE_OUT_TIME = 0.15f;
    const float FADE_IN_TIME = 0.12f;
    const float SLIDE_TIME = 0.25f;

    // Hitbox module (with fade animation)
    if (g_hitboxEnabled || g_hitboxDisableTime > 0) {
        float timeSinceEnable = (float)(GetTickCount64() - g_hitboxEnableTime) / 1000.0f;
        float timeSinceDisable = (float)(GetTickCount64() - g_hitboxDisableTime) / 1000.0f;
        
        float hitboxAlpha = 255.0f;
        float slideOffset = 0.0f;  // Offset de desplazamiento
        
        if (g_hitboxEnabled) {
            // Fade in elegante
            hitboxAlpha = Animations::SmoothInertia(fminf(1.0f, timeSinceEnable / FADE_IN_TIME)) * 255.0f;
            // Slide in animation - desplazamiento desde -60px
            float slideProgress = fminf(1.0f, timeSinceEnable / SLIDE_TIME);
            slideOffset = Animations::SmoothInertia(slideProgress) * 60.0f - 60.0f;  // -60 a 0
        } else if (timeSinceDisable < FADE_OUT_TIME) {
            // Fade out elegante
            hitboxAlpha = Animations::SmoothInertia(1.0f - (timeSinceDisable / FADE_OUT_TIME)) * 255.0f;
        } else {
            g_hitboxDisableTime = 0;  // Reset tiempo
        }
        
        if (hitboxAlpha > 1.0f) {
            char hBuf[64];
            sprintf_s(hBuf, "Hitbox - %.2f", g_hitboxValue);
            float wH = ImGui::CalcTextSize(hBuf).x;
            float xPosH = arrayListStart.x + 290.0f - wH - 10;
            draw->AddText(ImVec2(xPosH + slideOffset - 1, yPos + 1), IM_COL32(0, 0, 0, 220), hBuf); // Sombra
            draw->AddText(ImVec2(xPosH + slideOffset, yPos), IM_COL32(255, 60, 60, (int)hitboxAlpha), hBuf);
            yPos += 18.0f;
            arrayListEnd.y = yPos;
        }
    }
}

void Hitbox::RenderMenu() {
    bool prev = g_hitboxEnabled;
    GUI::RenderCustomSwitch("Hitbox Expander", &g_hitboxEnabled);
    if (prev != g_hitboxEnabled) {
        if (g_hitboxEnabled) Enable(); else Disable();
    }
    
    if (GUI::BeginModuleSettings("Hitbox", &g_hitboxEnabled)) {
        if (ImGui::SliderFloat("Hitbox Size", &g_hitboxValue, 0.6f, 10.0f, "%.2f")) {
            if (g_hitboxCave) memcpy((BYTE*)g_hitboxCave + 1, &g_hitboxValue, 4);
        }
        GUI::EndModuleSettings();
    }
}
