/*
Under an4rch Development Public Source License 1.0
*/

#include "HighJump.hpp"
#include "../../Alloc/AllocateNear.hpp"
#include "../../PatternScan/PatternScan.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../GUI/GUI.hpp"
#include <windows.h>
#include <cstring>
#include <cstdio>

bool HighJump::g_enabled = false;
float HighJump::g_jumpValue = 5.0f;
uintptr_t HighJump::g_addr = 0;
void* HighJump::g_cave = nullptr;
float* HighJump::g_jumpValuePtr = nullptr;
BYTE HighJump::g_backup[5] = { 0 };
ULONGLONG HighJump::g_enableTime = 0;
ULONGLONG HighJump::g_disableTime = 0;

void HighJump::Initialize(uintptr_t gameBase) {
    g_enableTime = GetTickCount64();
}

void HighJump::ScanPattern(uintptr_t gameBase, size_t imageSize) {
    BYTE pattern[] = { 0xF3, 0x0F, 0x11, 0x4F, 0x34, 0x48, 0x8B, 0x87,
                       0x00, 0x00, 0x00, 0x00, 0x48, 0x2B, 0x87,
                       0x00, 0x00, 0x00, 0x00, 0x41, 0x8B, 0x49, 0x08 };
    // Search with wildcards: replace ** with 00 for scan, but we need a custom scan
    // Simpler: scan for the unique 5-byte prefix
    BYTE simplePattern[] = { 0xF3, 0x0F, 0x11, 0x4F, 0x34, 0x48, 0x8B, 0x87 };
    uintptr_t found = PatternScan::Scan(gameBase, imageSize, simplePattern, sizeof(simplePattern));
    if (found) {
        g_addr = found;
    }
}

void HighJump::Disable() {
    if (!g_addr || g_backup[0] == 0) return;
    g_disableTime = GetTickCount64();
    DWORD old;
    VirtualProtect((void*)g_addr, 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void*)g_addr, g_backup, 5);
    VirtualProtect((void*)g_addr, 5, old, &old);
}

void HighJump::Enable() {
    if (!g_addr) return;
    g_enableTime = GetTickCount64();
    if (!g_cave) g_cave = AllocateNear::Allocate(g_addr, 1024);
    if (!g_cave) return;

    memcpy(g_backup, (void*)g_addr, 5);

    // Build code cave
    // Original: movss [rdi+34],xmm1  (F3 0F 11 4F 34)
    // Modified: movss xmm1,[HighJumpValue] then movss [rdi+34],xmm1
    BYTE shellcode[64] = { 0 };
    int p = 0;

    // movss xmm1, [HighJumpValue]  ; F3 0F 10 0D XX XX XX XX
    shellcode[p++] = 0xF3;
    shellcode[p++] = 0x0F;
    shellcode[p++] = 0x10;
    shellcode[p++] = 0x0D;
    // RIP-relative offset: value is at (g_cave + p + 4 + offset_to_value)
    // We'll place the float value right after the jmp-back, so calculate offset
    // For now, placeholder - we fix after
    int valueOffset = p; // offset of the 4-byte displacement
    p += 4; // skip displacement

    // movss [rdi+34], xmm1  ; F3 0F 11 4F 34
    shellcode[p++] = 0xF3;
    shellcode[p++] = 0x0F;
    shellcode[p++] = 0x11;
    shellcode[p++] = 0x4F;
    shellcode[p++] = 0x34;

    // jmp return
    shellcode[p++] = 0xE9;
    int jmpBackPos = p;
    p += 4;

    // Place the float value here
    int valuePos = p;
    memcpy(&shellcode[p], &g_jumpValue, 4);
    p += 4;

    // Fix the RIP-relative displacement for movss xmm1,[value]
    // displacement = valueAddr - (instructionAddr + 4 + 3)
    // instructionAddr = g_cave + valueOffset (the displacement field position is at g_cave + valueOffset)
    // The instruction starts at g_cave + (valueOffset - 3) because opcode is 4 bytes before displacement
    // Actually: instruction at g_cave + (valueOffset - 3), disp at valueOffset, next instr at valueOffset + 4
    // displacement = valueAddr - (g_cave + valueOffset + 4)
    uintptr_t valueAddr = (uintptr_t)g_cave + valuePos;
    uintptr_t dispAddr = (uintptr_t)g_cave + valueOffset + 4; // address after the displacement field
    int32_t displacement = (int32_t)(valueAddr - dispAddr);
    memcpy(&shellcode[valueOffset], &displacement, 4);

    // Fix jmp back
    int32_t jmpBack = (int32_t)((g_addr + 5) - ((uintptr_t)g_cave + jmpBackPos + 4));
    memcpy(&shellcode[jmpBackPos], &jmpBack, 4);

    memcpy(g_cave, shellcode, p);

    // Store pointer to the value in the cave so we can update it dynamically
    g_jumpValuePtr = (float*)((uintptr_t)g_cave + valuePos);

    // Patch original: jmp newmem + nop
    DWORD old;
    VirtualProtect((void*)g_addr, 5, PAGE_EXECUTE_READWRITE, &old);
    BYTE patch[5] = { 0xE9, 0, 0, 0, 0 };
    int32_t jmpToCave = (int32_t)((uintptr_t)g_cave - (g_addr + 5));
    memcpy(&patch[1], &jmpToCave, 4);
    memcpy((void*)g_addr, patch, 5);
    VirtualProtect((void*)g_addr, 5, old, &old);
}

void HighJump::RenderMenu() {
    bool prev = g_enabled;
    GUI::RenderCustomSwitch("High Jump", &g_enabled);
    if (prev != g_enabled) {
        if (g_enabled) Enable(); else Disable();
    }

    if (GUI::BeginModuleSettings("HighJump", &g_enabled)) {
        if (GUI::RenderSlider("Jump Height", &g_jumpValue, 1.0f, 20.0f, "%.1f")) {
            if (g_jumpValuePtr) *g_jumpValuePtr = g_jumpValue;
        }
        GUI::EndModuleSettings();
    }
}
