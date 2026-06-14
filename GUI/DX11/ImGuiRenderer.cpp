/*
Under an4rch Development Public Source License 1.0
*/

#include "ImGuiRenderer.hpp"

namespace ImGuiDX11 {
    void SyncImGuiAndDX11(IDXGISwapChain* pSwapChain, float& width, float& height)
    {
        DXGI_SWAP_CHAIN_DESC sd;
        pSwapChain->GetDesc(&sd);

        // Fallback for full screen edge cases
        if (width <= 0 || height <= 0)
        {
            RECT rect;
            GetClientRect(sd.OutputWindow, &rect);
            width  = (float)(rect.right - rect.left);
            height = (float)(rect.bottom - rect.top);
        }

        // Real viewport in pixels
        D3D11_VIEWPORT vp;
        vp.Width    = width;
        vp.Height   = height;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = 0;
        vp.TopLeftY = 0;

        pContext->RSSetViewports(1, &vp);

        // ImGui DisplaySize matches backbuffer size
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(width, height);

        // DisplayFramebufferScale = 1.0f (backbuffer es la fuente de verdad)
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    }
}
