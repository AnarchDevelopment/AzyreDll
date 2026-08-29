/*
Under an4rch Development Public Source License 1.0
*/

#include "ImGuiRenderer.hpp"
#include "../../Modules/Globals.hpp"
#include "../../Modules/ModuleHeader.hpp"
#include "../../Modules/ModuleManager.hpp"
#include "../../Modules/Splash/Splash.hpp"
#include "../../GUI/GUI.hpp"
#include "../../Input/Input.hpp"
#include "../../ImGui/backend/imgui_impl_dx11.h"
#include "../../ImGui/backend/imgui_impl_win32.h"
#include "../../Utils/WinRTTitle.hpp"
#include <psapi.h>

#pragma comment(lib, "psapi.lib")

// Release the cached swap-chain render target (called on resize / shutdown)
void CleanupRenderTarget() {
    if (mainRenderTargetView) {
        mainRenderTargetView->Release();
        mainRenderTargetView = NULL;
    }
}

namespace ImGuiDX11 {
    void Initialize(IDXGISwapChain* pSwapChain) {
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice))) {
            pDevice->GetImmediateContext(&pContext);
            UnlockFPS::OnDeviceReady(pDevice);
            DXGI_SWAP_CHAIN_DESC sd;
            pSwapChain->GetDesc(&sd);
            g_window = sd.OutputWindow;
            if (!g_window) g_window = GetForegroundWindow();

            WinRTTitle::SetTitle(g_window);

            ImGui::CreateContext();
            ImGui_ImplWin32_Init(g_window);

            ImGui_ImplDX11_Init(pDevice, pContext);
            GUI::LoadFont();
            ImGui_ImplDX11_CreateDeviceObjects();
            GUI::ApplyTheme();

            oWndProc = (WNDPROC)SetWindowLongPtr(g_window, GWLP_WNDPROC, (LONG_PTR)Input::WndProc);
            g_gameBase = (uintptr_t)GetModuleHandleA(NULL);

            // Texture/Resource Initialization (Must be on render thread)
            Watermark::InitializeTextures();
            GUI::InitializeTextures();
            ClickGUI::InitializeBlurShaders(pDevice);

            // Centralized Module Initialization
            HMODULE hModule = GetModuleHandleA(NULL);
            MODULEINFO mi;
            GetModuleInformation(GetCurrentProcess(), hModule, &mi, sizeof(mi));
            Module::Initialize(g_gameBase, mi.SizeOfImage, &g_renderInfoHud, &g_watermarkHud, &g_keystrokesHud, &g_cpsHud, &g_fpsOverlayHud, &g_pingHud, &g_playerInfoHud, &g_mouseStrokesHud);

            Watermark::g_watermarkEnableTime = GetTickCount64();
            Watermark::g_watermarkAnim = 1.0f;

            g_notifStart = GetTickCount64();
            g_lastTime = GetTickCount64();

            // Startup splash: plays right after assets/offsets are loaded
            Splash::Begin();
        }
    }

    void PrepareFrame(IDXGISwapChain* pSwapChain, float& width, float& height) {
        width = 0;
        height = 0;

        ID3D11Texture2D* pBackBuffer = NULL;
        if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer))) {
            D3D11_TEXTURE2D_DESC desc;
            pBackBuffer->GetDesc(&desc);
            width = (float)desc.Width;
            height = (float)desc.Height;
            if (!mainRenderTargetView) pDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
            pBackBuffer->Release();
        }
    }

    void RenderFrame() {
        ImGui::Render();
        pContext->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

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
