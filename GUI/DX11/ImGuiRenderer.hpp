/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include "ImGui/imgui.h"

extern ID3D11DeviceContext* pContext;

namespace ImGuiDX11 {
    void SyncImGuiAndDX11(IDXGISwapChain* pSwapChain, float& width, float& height);
}
