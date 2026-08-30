/*
Under an4rch Development Public Source License 1.0
*/

#include "NoHurtCam.hpp"
#include "../../PatternScan/PatternScan.hpp"
#include "../../Alloc/AllocateNear.hpp"
#include "../../Terminal/Terminal.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../GUI/GUI.hpp"
#include <windows.h>
#include <cstring>

bool NoHurtCam::g_noHurtCamEnabled = false;
uintptr_t NoHurtCam::g_noHurtCamAddr = 0;
void* NoHurtCam::g_noHurtCamCave = nullptr;
BYTE NoHurtCam::g_noHurtCamBackup[10] = { 0 };

void NoHurtCam::ScanPattern(uintptr_t gameBase, size_t imageSize) {
    if (g_noHurtCamAddr) return;

    // Use the exact offset from the CE script first
    if (imageSize > 0x4D9628) {
        uintptr_t target = gameBase + 0x4D9618;
        g_noHurtCamAddr = target;
    }

    // Fallback: pattern scan (may find wrong instance)
    if (!g_noHurtCamAddr) {
        BYTE pattern[] = { 0xC7, 0x81, 0x24, 0x02, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00 };
        g_noHurtCamAddr = PatternScan::Scan(gameBase, imageSize, pattern, sizeof(pattern));
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "[NoHurtCam] base=%p addr=%p", (void*)gameBase, (void*)g_noHurtCamAddr);
    Terminal::AddOutput(buf);
}

void NoHurtCam::Initialize(uintptr_t gameBase) {
    (void)gameBase;
}

void NoHurtCam::Enable() {
    if (!g_noHurtCamAddr) { Terminal::AddOutput("[NoHurtCam] addr=0"); return; }
    if (!g_noHurtCamCave) g_noHurtCamCave = AllocateNear::Allocate(g_noHurtCamAddr, 1024);
    if (!g_noHurtCamCave) { Terminal::AddOutput("[NoHurtCam] cave alloc fail"); return; }

    memcpy(g_noHurtCamBackup, (void*)g_noHurtCamAddr, 10);

    BYTE shellcode[64] = { 0 };
    int p = 0;
    shellcode[p++] = 0xC7; shellcode[p++] = 0x81;
    shellcode[p++] = 0x24; shellcode[p++] = 0x02;
    shellcode[p++] = 0x00; shellcode[p++] = 0x00;
    shellcode[p++] = 0x00; shellcode[p++] = 0x00;
    shellcode[p++] = 0x00; shellcode[p++] = 0x00;
    shellcode[p++] = 0xE9;
    uintptr_t retAddr = g_noHurtCamAddr + 10;
    int32_t jmpBack = (int32_t)(retAddr - ((uintptr_t)g_noHurtCamCave + p + 4));
    memcpy(&shellcode[p], &jmpBack, 4); p += 4;
    memcpy(g_noHurtCamCave, shellcode, p);

    DWORD old;
    VirtualProtect((void*)g_noHurtCamAddr, 10, PAGE_EXECUTE_READWRITE, &old);
    BYTE patch[10] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90, 0x90 };
    int32_t jmpToCave = (int32_t)((uintptr_t)g_noHurtCamCave - (g_noHurtCamAddr + 5));
    memcpy(&patch[1], &jmpToCave, 4);
    memcpy((void*)g_noHurtCamAddr, patch, 10);
    VirtualProtect((void*)g_noHurtCamAddr, 10, old, &old);
}

void NoHurtCam::Disable() {
    if (!g_noHurtCamAddr) return;
    bool hasBackup = false;
    for (int i = 0; i < 10; i++) { if (g_noHurtCamBackup[i] != 0) { hasBackup = true; break; } }
    if (!hasBackup) return;
    DWORD old;
    VirtualProtect((void*)g_noHurtCamAddr, 10, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void*)g_noHurtCamAddr, g_noHurtCamBackup, 10);
    VirtualProtect((void*)g_noHurtCamAddr, 10, old, &old);
}

void NoHurtCam::RenderMenu() {
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Unstable Feature - May Cause Discomfort\nNoHurtCam removes the hurt camera shake effect");
    bool prev = g_noHurtCamEnabled;
    GUI::RenderCustomSwitch("NoHurtCam", &g_noHurtCamEnabled);
    if (prev != g_noHurtCamEnabled) {
        if (g_noHurtCamEnabled) Enable(); else Disable();
    }
}
