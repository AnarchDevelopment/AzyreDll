/*
Under an4rch Development Public Source License 1.0
*/

#include "Fly.hpp"
#include "../../Alloc/AllocateNear.hpp"
#include "../../PatternScan/PatternScan.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../GUI/GUI.hpp"
#include <windows.h>
#include <cstring>
#include <cstdio>

// Static member initialization
bool Fly::g_flyEnabled = false;
bool Fly::g_flyPatched = false;
uintptr_t Fly::g_flyAddr = 0;
void* Fly::g_flyCave = nullptr;
BYTE Fly::g_flyBackup[7] = { 0 };
ULONGLONG Fly::g_flyEnableTime = 0;
ULONGLONG Fly::g_flyDisableTime = 0;

// Fallback offset for the known build (Cheat Engine script reference)
static const uintptr_t kFlyFallbackOffset = 0x4FD73B;

void Fly::ScanPattern(uintptr_t gameBase, size_t imageSize) {
    if (g_flyAddr) return;
    // Unique context around the injection point:
    //   80 B9 31 0E 00 00 00        cmp byte ptr [rcx+00000E31], 00   <-- inject here
    //   48 8B D9                    mov rbx, rcx
    //   0F 29 70                    movaps [rax-18], xmm6
    BYTE pattern[] = { 0x80, 0xB9, 0x31, 0x0E, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xD9, 0x0F, 0x29, 0x70 };
    uintptr_t found = PatternScan::Scan(gameBase, imageSize, pattern, sizeof(pattern));
    if (found) {
        g_flyAddr = found;
    } else {
        g_flyAddr = gameBase + kFlyFallbackOffset;
    }
    char dbg[256];
    sprintf_s(dbg, "AEGLE Fly: target = 0x%llX (%s)",
        (unsigned long long)g_flyAddr, found ? "pattern" : "FALLBACK");
    OutputDebugStringA(dbg);
}

void Fly::Initialize(uintptr_t gameBase) {
    // Pattern scanning is done via ScanPattern (called from Module::Initialize)
    (void)gameBase;
}

void Fly::Disable() {
    if (!g_flyAddr || !g_flyPatched || g_flyBackup[0] == 0) return;
    g_flyDisableTime = GetTickCount64();
    g_flyPatched = false;
    DWORD old;
    VirtualProtect((void*)g_flyAddr, 7, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void*)g_flyAddr, g_flyBackup, 7);
    VirtualProtect((void*)g_flyAddr, 7, old, &old);
}

void Fly::Enable() {
    if (!g_flyAddr || g_flyPatched) return;
    g_flyEnableTime = GetTickCount64();
    g_flyPatched = true;
    if (!g_flyCave) g_flyCave = AllocateNear::Allocate(g_flyAddr, 1024);
    if (!g_flyCave) { g_flyPatched = false; return; }

    // Backup the original instruction bytes BEFORE patching
    memcpy(g_flyBackup, (void*)g_flyAddr, 7);

    BYTE shellcode[64] = { 0 };
    int p = 0;

    // test eax, eax   ; 85 C0
    // rax = rsp at the injection point (mov rax,rsp) and is never 0, so ZF=0.
    // The original `cmp byte ptr [rcx+00000E31],00` only set the flags; the
    // CE script re-pointed it at the stack hoping for a nonzero byte. Forcing
    // the "flag != 0" path (ZF=0) deterministically is what actually unlocks
    // the flight branch of the following code.
    shellcode[p++] = 0x85; shellcode[p++] = 0xC0;
    // jmp return
    shellcode[p++] = 0xE9;
    int32_t relBack = (int32_t)((g_flyAddr + 7) - ((uintptr_t)g_flyCave + p + 4));
    memcpy(&shellcode[p], &relBack, 4); p += 4;

    memcpy(g_flyCave, shellcode, p);

    // Patch the injection point with a jump to the cave (7-byte region: E9 rel32 + 2 NOPs)
    DWORD old;
    VirtualProtect((void*)g_flyAddr, 7, PAGE_EXECUTE_READWRITE, &old);
    BYTE patch[7] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90 };
    int32_t jmpToCave = (int32_t)((uintptr_t)g_flyCave - (g_flyAddr + 5));
    memcpy(&patch[1], &jmpToCave, 4);
    memcpy((void*)g_flyAddr, patch, 7);
    VirtualProtect((void*)g_flyAddr, 7, old, &old);

    char dbg[256];
    sprintf_s(dbg, "AEGLE Fly: patched 0x%llX -> cave 0x%llX | orig=%02X %02X %02X %02X %02X %02X %02X | jmp=%02X %02X %02X %02X %02X %02X %02X",
        (unsigned long long)g_flyAddr, (unsigned long long)g_flyCave,
        g_flyBackup[0], g_flyBackup[1], g_flyBackup[2], g_flyBackup[3],
        g_flyBackup[4], g_flyBackup[5], g_flyBackup[6],
        patch[0], patch[1], patch[2], patch[3], patch[4], patch[5], patch[6]);
    OutputDebugStringA(dbg);
}

void Fly::Shutdown() {
    Disable();
}

void Fly::RenderMenu() {
    bool prev = g_flyEnabled;
    GUI::RenderCustomSwitch("Fly", &g_flyEnabled);
    if (prev != g_flyEnabled) {
        if (g_flyEnabled) Enable(); else Disable();
    }

    if (GUI::BeginModuleSettings("Fly", &g_flyEnabled)) {
        ImGui::TextDisabled("Disables the game's flight check for creative fly.");
        GUI::EndModuleSettings();
    }
}
