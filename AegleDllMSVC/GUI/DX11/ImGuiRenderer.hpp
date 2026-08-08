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

    // One-time boot: create the ImGui context, hook the window proc and init modules
    void Initialize(IDXGISwapChain* pSwapChain);

    // Per-frame setup: refresh the render target and report the backbuffer size
    void PrepareFrame(IDXGISwapChain* pSwapChain, float& width, float& height);

    // Per-frame draw: render ImGui draw data to the swap chain
    void RenderFrame();
}
