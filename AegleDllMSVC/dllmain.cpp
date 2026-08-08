/*
Under an4rch Development Public Source License 1.0
*/

#include <windows.h>
#include "Modules/Globals.hpp"
#include "Core/Present.hpp"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "ws2_32.lib")

BOOL WINAPI DllMain(HMODULE hMod, DWORD dwReason, LPVOID lpReserved) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        g_hModule = hMod;
        DisableThreadLibraryCalls(hMod);
        CreateThread(NULL, 0, Present::MainThread, NULL, 0, NULL);
    }
    return TRUE;
}
