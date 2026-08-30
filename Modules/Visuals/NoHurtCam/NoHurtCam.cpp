/*
Under an4rch Development Public Source License 1.0
*/

#include "NoHurtCam.hpp"
#include "../../PatternScan/PatternScan.hpp"
#include "../../Alloc/AllocateNear.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../GUI/GUI.hpp"
#include <windows.h>
#include <cstring>

bool NoHurtCam::g_noHurtCamEnabled = false;
uintptr_t NoHurtCam::g_noHurtCamAddr = 0;
void* NoHurtCam::g_noHurtCamCave = nullptr;
BYTE NoHurtCam::g_noHurtCamBackup[8] = { 0 };

void NoHurtCam::ScanPattern(uintptr_t gameBase, size_t imageSize) {
    if (g_noHurtCamAddr) return;

    // Pattern: movd xmm7,[rbx+00000224]
    // 66 0F 6E BB 24 02 00 00  (8 bytes)
    BYTE pattern[] = { 0x66, 0x0F, 0x6E, 0xBB, 0x24, 0x02, 0x00, 0x00 };
    g_noHurtCamAddr = PatternScan::Scan(gameBase, imageSize, pattern, sizeof(pattern));

}


void NoHurtCam::Initialize(uintptr_t gameBase) {
    (void)gameBase;
}

void NoHurtCam::Enable() {
    if (!g_noHurtCamAddr) return;
    if (!g_noHurtCamCave) g_noHurtCamCave = AllocateNear::Allocate(g_noHurtCamAddr, 1024);
    if (!g_noHurtCamCave) return;

    // Backup original 8 bytes: 66 0F 6E BB 24 02 00 00  (movd xmm7,[rbx+00000224])
    memcpy(g_noHurtCamBackup, (void*)g_noHurtCamAddr, 8);

    // Build cave shellcode:
    //   xorps xmm7,xmm7   ; 0F 57 FF  (3 bytes) — zeroes xmm7 so the engine sees timer=0
    //   jmp return         ; E9 ?? ?? ?? ??  (5 bytes)
    BYTE shellcode[64] = { 0 };
    int p = 0;

    // xorps xmm7,xmm7
    shellcode[p++] = 0x0F;
    shellcode[p++] = 0x57;
    shellcode[p++] = 0xFF;

    // jmp return  (back to NoHurtCam + 8)
    shellcode[p++] = 0xE9;
    uintptr_t retAddr = g_noHurtCamAddr + 8;
    int32_t jmpBack = (int32_t)(retAddr - ((uintptr_t)g_noHurtCamCave + p + 4));
    memcpy(&shellcode[p], &jmpBack, 4); p += 4;

    DWORD oldCave;
    VirtualProtect(g_noHurtCamCave, p, PAGE_EXECUTE_READWRITE, &oldCave);
    memcpy(g_noHurtCamCave, shellcode, p);
    VirtualProtect(g_noHurtCamCave, p, oldCave, &oldCave);

    // Patch original site: jmp newmem + nop 3
    //   E9 ?? ?? ?? ??  +  90 90 90  = 8 bytes
    DWORD old;
    VirtualProtect((void*)g_noHurtCamAddr, 8, PAGE_EXECUTE_READWRITE, &old);
    BYTE patch[8] = { 0xE9, 0x00, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90 };
    int32_t jmpToCave = (int32_t)((uintptr_t)g_noHurtCamCave - (g_noHurtCamAddr + 5));
    memcpy(&patch[1], &jmpToCave, 4);
    memcpy((void*)g_noHurtCamAddr, patch, 8);
    VirtualProtect((void*)g_noHurtCamAddr, 8, old, &old);


}

void NoHurtCam::Disable() {
    if (!g_noHurtCamAddr) return;
    bool hasBackup = false;
    for (int i = 0; i < 8; i++) { if (g_noHurtCamBackup[i] != 0) { hasBackup = true; break; } }
    if (!hasBackup) return;
    DWORD old;
    VirtualProtect((void*)g_noHurtCamAddr, 8, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void*)g_noHurtCamAddr, g_noHurtCamBackup, 8);
    VirtualProtect((void*)g_noHurtCamAddr, 8, old, &old);

}

void NoHurtCam::RenderMenu() {
    ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.4f, 1.0f), "Perfect NoHurtCam\nPreserves Hurt Color and Immunity");
    bool prev = g_noHurtCamEnabled;
    GUI::RenderCustomSwitch("NoHurtCam", &g_noHurtCamEnabled);
    if (prev != g_noHurtCamEnabled) {
        if (g_noHurtCamEnabled) Enable(); else Disable();
    }
}
