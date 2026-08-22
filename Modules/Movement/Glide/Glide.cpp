/*
Under an4rch Development Public Source License 1.0
*/

#include "Glide.hpp"
#include "../../Alloc/AllocateNear.hpp"
#include "../../PatternScan/PatternScan.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../GUI/GUI.hpp"
#include <windows.h>
#include <cstring>
#include <cstdio>

// Static member initialization
bool Glide::g_glideEnabled = false;
bool Glide::g_glidePatched = false;
float Glide::g_glideSpeed = -0.1f;
uintptr_t Glide::g_glideAddr = 0;
void* Glide::g_glideCave = nullptr;
int Glide::g_glideSpeedOffset = -1;
BYTE Glide::g_glideBackup[5] = { 0 };
ULONGLONG Glide::g_glideEnableTime = 0;
ULONGLONG Glide::g_glideDisableTime = 0;

// Fallback offset for the known build (Cheat Engine script reference)
static const uintptr_t kGlideFallbackOffset = 0x4D740B;

void Glide::ScanPattern(uintptr_t gameBase, size_t imageSize) {
    if (g_glideAddr) return;
    // Unique context around the injection point:
    //   0F 28 CE              movaps xmm1, xmm6
    //   F3 0F 11 47 34        movss [rdi+34], xmm0   <-- inject here
    //   FF 90 C8 04 00 00     call qword ptr [rax+000004C8]
    BYTE pattern[] = { 0x0F, 0x28, 0xCE, 0xF3, 0x0F, 0x11, 0x47, 0x34, 0xFF, 0x90, 0xC8, 0x04, 0x00, 0x00 };
    uintptr_t found = PatternScan::Scan(gameBase, imageSize, pattern, sizeof(pattern));
    if (found) {
        g_glideAddr = found + 3;
    } else {
        g_glideAddr = gameBase + kGlideFallbackOffset;
    }
}

void Glide::Initialize(uintptr_t gameBase) {
    // Pattern scanning is done via ScanPattern (called from Module::Initialize)
    (void)gameBase;
}

void Glide::UpdateSpeed() {
    if (!g_glideCave || g_glideSpeedOffset < 0) return;
    memcpy((BYTE*)g_glideCave + g_glideSpeedOffset, &g_glideSpeed, 4);
}

void Glide::Disable() {
    if (!g_glideAddr || !g_glidePatched || g_glideBackup[0] == 0) return;
    g_glideDisableTime = GetTickCount64();
    g_glidePatched = false;
    DWORD old;
    VirtualProtect((void*)g_glideAddr, 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void*)g_glideAddr, g_glideBackup, 5);
    VirtualProtect((void*)g_glideAddr, 5, old, &old);
}

void Glide::Enable() {
    if (!g_glideAddr || g_glidePatched) return;
    g_glideEnableTime = GetTickCount64();
    g_glidePatched = true;
    if (!g_glideCave) g_glideCave = AllocateNear::Allocate(g_glideAddr, 1024);
    if (!g_glideCave) { g_glidePatched = false; return; }

    // Backup the original instruction bytes BEFORE patching
    memcpy(g_glideBackup, (void*)g_glideAddr, 5);

    BYTE shellcode[64] = { 0 };
    int p = 0;

    // xorps xmm3, xmm3                     ; 0F 57 DB
    shellcode[p++] = 0x0F; shellcode[p++] = 0x57; shellcode[p++] = 0xDB;
    // comiss xmm3, [rdi+34]                ; 0F 2F 5F 34   (CF set if [rdi+34] > 0)
    shellcode[p++] = 0x0F; shellcode[p++] = 0x2F; shellcode[p++] = 0x5F; shellcode[p++] = 0x34;
    // ja glide                              ; 77 0A   (jump if [rdi+34] < 0 -> falling)
    shellcode[p++] = 0x77; shellcode[p++] = 0x0A;

    // code: original instruction
    shellcode[p++] = 0xF3; shellcode[p++] = 0x0F; shellcode[p++] = 0x11; shellcode[p++] = 0x47; shellcode[p++] = 0x34;
    // jmp return
    shellcode[p++] = 0xE9;
    int32_t relBack1 = (int32_t)((g_glideAddr + 5) - ((uintptr_t)g_glideCave + p + 4));
    memcpy(&shellcode[p], &relBack1, 4); p += 4;

    // glide: mov dword ptr [rdi+34], g_glideSpeed
    shellcode[p++] = 0xC7; shellcode[p++] = 0x47; shellcode[p++] = 0x34;
    g_glideSpeedOffset = p;
    memcpy(&shellcode[p], &g_glideSpeed, 4); p += 4;
    // jmp return
    shellcode[p++] = 0xE9;
    int32_t relBack2 = (int32_t)((g_glideAddr + 5) - ((uintptr_t)g_glideCave + p + 4));
    memcpy(&shellcode[p], &relBack2, 4); p += 4;

    memcpy(g_glideCave, shellcode, p);

    // Patch the injection point with a jump to the cave
    DWORD old;
    VirtualProtect((void*)g_glideAddr, 5, PAGE_EXECUTE_READWRITE, &old);
    BYTE patch[5] = { 0xE9, 0, 0, 0, 0 };
    int32_t jmpToCave = (int32_t)((uintptr_t)g_glideCave - (g_glideAddr + 5));
    memcpy(&patch[1], &jmpToCave, 4);
    memcpy((void*)g_glideAddr, patch, 5);
    VirtualProtect((void*)g_glideAddr, 5, old, &old);
}

void Glide::Shutdown() {
    Disable();
}

void Glide::RenderMenu() {
    bool prev = g_glideEnabled;
    GUI::RenderCustomSwitch("Glide", &g_glideEnabled);
    if (prev != g_glideEnabled) {
        if (g_glideEnabled) Enable(); else Disable();
    }

    if (GUI::BeginModuleSettings("Glide", &g_glideEnabled)) {
        if (GUI::RenderSlider("##glideSpeed", &g_glideSpeed, -0.5f, 0.0f, "Fall Speed: %.2f")) {
            if (g_glideEnabled) UpdateSpeed();
        }
        ImGui::TextDisabled("Clamps your falling velocity for a slow glide.");
        GUI::EndModuleSettings();
    }
}
