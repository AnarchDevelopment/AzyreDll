/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

namespace Present {
    // Client bootstrap thread (created by DllMain)
    DWORD WINAPI MainThread(LPVOID lpReserved);

    // Per-frame render pipeline (called from the swap-chain Present hook)
    HRESULT Run(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
}
