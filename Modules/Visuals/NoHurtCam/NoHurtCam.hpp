/*
Under an4rch Development Public Source License 1.0
*/

#pragma once
#include <cstdint>
#include <windows.h>

class NoHurtCam {
public:
    static bool g_noHurtCamEnabled;
    static uintptr_t g_noHurtCamAddr;
    static void* g_noHurtCamCave;
    static BYTE g_noHurtCamBackup[10];

    static void Initialize(uintptr_t gameBase);
    static void ScanPattern(uintptr_t gameBase, size_t imageSize);
    static void Enable();
    static void Disable();
    static void RenderMenu();
    static bool IsEnabled() { return g_noHurtCamEnabled; }
};
