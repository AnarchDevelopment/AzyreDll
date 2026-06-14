/*
Under an4rch Development Public Source License 1.0
*/

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <psapi.h>
#include <functional>


#include "minhook/MinHook.h"
#include "ImGui/imgui.h"
#include "ImGui/backend/imgui_impl_dx11.h"
#include "ImGui/backend/imgui_impl_win32.h"
#include "Modules/ModuleHeader.hpp"
#include "Modules/ModuleManager.hpp"
#include "Modules/PatternScan/PatternScan.hpp"
#include "Modules/Globals.hpp"
#include "Animations/Animations.hpp"
#include "GUI/GUI.hpp"
#include "GUI/DX11/ImGuiRenderer.hpp"
#include "ArrayList/ArrayList.hpp"
#include "Hook/Hook.hpp"
#include "Input/Input.hpp"
#include "Config/ConfigManager.hpp"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "psapi.lib")

// TODO: Refactor into multiple files and classes for better organization and maintainability

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

ImGuiConfigFlags g_imguiConfigFlags = ImGuiConfigFlags_None;
WNDPROC oWndProc = NULL;
HMODULE g_hModule = NULL;

ID3D11Device* pDevice = NULL;
ID3D11DeviceContext* pContext = NULL;
ID3D11RenderTargetView* mainRenderTargetView = NULL;
ma_engine g_audioEngine;
HWND g_window = NULL;

bool g_showMenu = false;
bool g_RequestUnload = false;
bool g_wasInWorld = false;
float g_menuAnim = 0.0f;
ULONGLONG g_lastTime = 0, g_lastToggle = 0, g_notifStart = 0;
bool g_vsync = false;
float g_lastW = 0, g_lastH = 0;

// Tab animation
int g_currentTab = 0;
int g_previousTab = 0;
ULONGLONG g_tabChangeTime = 0;
float g_tabAnim = 0.0f;
bool g_firstTabOpen = true;
uintptr_t g_gameBase = 0;
bool IsWindowsCursorVisible() {
    CURSORINFO ci = { 0 };
    ci.cbSize = sizeof(CURSORINFO);
    if (GetCursorInfo(&ci)) {
        return (ci.flags & CURSOR_SHOWING) != 0;
    }
    return false;
}

bool IsInWorld() {
    return !IsWindowsCursorVisible();
}
HudElement g_watermarkHud = { ImVec2(10, 10), ImVec2(400, 80) };
HudElement g_renderInfoHud = { ImVec2(10, 100), ImVec2(220, 120) };
HudElement g_arrayListHud = { ImVec2(0, 10), ImVec2(300, 400) };
HudElement g_keystrokesHud = { ImVec2(30, 0), ImVec2(140, 150) };
HudElement g_cpsHud = { ImVec2(500, 400), ImVec2(80, 30) };
HudElement g_fpsOverlayHud = { ImVec2(10, 250), ImVec2(100, 35) };
HudElement g_pingHud = { ImVec2(0, 0), ImVec2(130, 28) };

LRESULT CALLBACK KeyboardBlockHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0) {
        return CallNextHookEx(Input::g_keyboardHook, nCode, wParam, lParam);
    }
    
    int result = Input::KeyboardBlockHookProc(nCode, wParam, lParam, g_showMenu);
    if (result == 1) {
        return 1;  // Block
    } else {
        return CallNextHookEx(Input::g_keyboardHook, nCode, wParam, lParam);
    }
}

ImVec4 LerpImVec4(ImVec4 a, ImVec4 b, float t) {
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t
    );
}

void ImageWithOpacity(ID3D11ShaderResourceView* srv, ImVec2 size, float opacity) {
    if (!srv || opacity <= 0.0f) {
        return;
    }

    opacity = opacity > 1.0f ? 1.0f : (opacity < 0.0f ? 0.0f : opacity);
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    ImVec2 pos = { 0, 0 };
    ImU32 col = IM_COL32(255, 255, 255, static_cast<int>(opacity * 255.0f));
    draw_list->AddImage((ImTextureID)srv, pos, ImVec2(pos.x + size.x, pos.y + size.y), ImVec2(0, 0), ImVec2(1, 1), col);
}

// -----------------------------------------------------------------------------------------------------

LRESULT CALLBACK hkWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return true;

    if (uMsg == WM_ACTIVATEAPP && wParam == TRUE) {
        // Force audio engine start when game regains focus
        ma_engine_start(&g_audioEngine);
        ma_device* pDev = ma_engine_get_device(&g_audioEngine);
        if (pDev) ma_device_start(pDev);
    }

    if (g_showMenu) {
        switch (uMsg) {
            case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
            case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_MBUTTONDOWN:
            case WM_MBUTTONUP: case WM_MOUSEWHEEL: case WM_INPUT: 
                return 1;
        }
    }
    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}

void CleanupRenderTarget() {
    if (mainRenderTargetView) { 
        mainRenderTargetView->Release(); 
        mainRenderTargetView = NULL; 
    }
}
HRESULT STDMETHODCALLTYPE hkPresent_Impl(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    if (UnlockFPS::g_unlockFpsEnabled) {
        SyncInterval = 0;
        Flags |= 0x0200; // DXGI_PRESENT_ALLOW_TEARING
    } else if (!g_vsync) {
        SyncInterval = 0;
    }
        
    if (!pDevice) {
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice))) {
            pDevice->GetImmediateContext(&pContext);
            DXGI_SWAP_CHAIN_DESC sd;
            pSwapChain->GetDesc(&sd);
            g_window = sd.OutputWindow;
            if (!g_window) g_window = GetForegroundWindow();

            ImGui::CreateContext();
            ImGui_ImplWin32_Init(g_window);
            ImGui_ImplDX11_Init(pDevice, pContext);
            GUI::LoadFont();
            ImGui_ImplDX11_CreateDeviceObjects();
            GUI::ApplyTheme();
            
            oWndProc = (WNDPROC)SetWindowLongPtr(g_window, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
            g_gameBase = (uintptr_t)GetModuleHandleA(NULL);
            
            // Texture/Resource Initialization (Must be on render thread)
            Watermark::InitializeTextures();
            
            // Centralized Module Initialization
            Module::Initialize(g_gameBase, &g_renderInfoHud, &g_watermarkHud, &g_keystrokesHud, &g_cpsHud, &g_fpsOverlayHud, &g_pingHud);
            
            Watermark::g_watermarkEnableTime = GetTickCount64();
            Watermark::g_watermarkAnim = 1.0f;
            
            HMODULE hModule = GetModuleHandleA(NULL);
            MODULEINFO mi;
            GetModuleInformation(GetCurrentProcess(), hModule, &mi, sizeof(mi));
            if (!AutoSprint::g_autoSprintAddr) {
                BYTE pattern[] = {0x0F, 0xB6, 0x41, 0x63, 0x48, 0x8D, 0x2D, 0x39, 0xE0, 0xC3, 0x00};
                AutoSprint::g_autoSprintAddr = PatternScan::Scan(g_gameBase, mi.SizeOfImage, pattern, sizeof(pattern));
            }
            if (!FullBright::g_fullBrightAddr) {
                BYTE pattern[] = {0xF3, 0x0F, 0x10, 0x80, 0xA0, 0x01, 0x00, 0x00};
                FullBright::g_fullBrightAddr = PatternScan::Scan(g_gameBase, mi.SizeOfImage, pattern, sizeof(pattern));
            }

            g_notifStart = GetTickCount64();
            g_lastTime = GetTickCount64();
        }
    }

    ID3D11Texture2D* pBackBuffer = NULL;
    float sw = 0, sh = 0;
    if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer))) {
        D3D11_TEXTURE2D_DESC desc;
        pBackBuffer->GetDesc(&desc);
        sw = (float)desc.Width; 
        sh = (float)desc.Height;
        if (!mainRenderTargetView) pDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
        if (MotionBlur::g_motionBlurEnabled) {
            int maxFrames = 1;
            if (MotionBlur::g_blurType == "Time Aware Blur") {
                maxFrames = (int)round(MotionBlur::g_maxHistoryFrames);
            } else if (MotionBlur::g_blurType == "Real Motion Blur") {
                maxFrames = 8;
            } else {
                maxFrames = (int)round(MotionBlur::g_blurIntensity);
            }
            
            if (maxFrames <= 0) maxFrames = 4;
            if (maxFrames > 16) maxFrames = 16;
            
            MotionBlur::InitializeBackbufferStorage(maxFrames);
            ID3D11ShaderResourceView* srv = MotionBlur::CopyBackbufferToSRV(pDevice, pContext, pSwapChain);
            if (srv) {
                if ((int)MotionBlur::g_previousFrames.size() >= maxFrames) {
                    if (MotionBlur::g_previousFrames[0]) MotionBlur::g_previousFrames[0]->Release();
                    MotionBlur::g_previousFrames.erase(MotionBlur::g_previousFrames.begin());
                    MotionBlur::g_frameTimestamps.erase(MotionBlur::g_frameTimestamps.begin());
                }
                
                MotionBlur::g_previousFrames.push_back(srv);
                MotionBlur::g_frameTimestamps.push_back((float)GetTickCount64() / 1000.0f);
            }
        }
        
        pBackBuffer->Release();
    }

    if (sw <= 0) return Hook::oPresent(pSwapChain, 0, Flags);
    if (((GetAsyncKeyState(VK_INSERT) & 0x8000) || (GetAsyncKeyState(VK_RSHIFT) & 0x8000)) && (GetTickCount64() - g_lastToggle) > 400) {
        g_showMenu = !g_showMenu;
        GUI::g_showMenu = g_showMenu;
        g_lastToggle = GetTickCount64();
        
        if (g_showMenu) {
            Input::BlockGameInput();
            Hook::oClipCursor(NULL);
            g_wasInWorld = IsInWorld();
            while (ShowCursor(TRUE) < 0);
        } else {
            Input::UnblockGameInput();
            if (g_wasInWorld) {
                while (ShowCursor(FALSE) >= 0);
            }
        }
    }

    // Sync menu state if closed via the GUI Close ("X") button
    if (g_showMenu && !GUI::g_showMenu) {
        g_showMenu = false;
        Input::UnblockGameInput();
        if (g_wasInWorld) {
            while (ShowCursor(FALSE) >= 0);
        }
    }
    static bool wasMotionBlurEnabled = false;
    if (!MotionBlur::g_motionBlurEnabled && wasMotionBlurEnabled) {
        MotionBlur::CleanupBackbufferStorage();
    }
    wasMotionBlurEnabled = MotionBlur::g_motionBlurEnabled;
    ULONGLONG now = GetTickCount64();
    float dt = (float)(now - g_lastTime) / 1000.0f;
    g_lastTime = now;
    GUI::UpdateAnimation(now, dt);
    float easedMenuAnim = Animations::SmoothInertia(GUI::g_menuAnim);
    Module::UpdateAnimation(now);

    ImGui_ImplDX11_NewFrame();
    // Set ImGui software cursor based on Windows cursor visibility

    Input::Update(g_window, sw, sh, g_showMenu, g_showMenu && !IsWindowsCursorVisible());

    CPSCounter::UpdateCPS(now, Input::IsLMBPressed(), Input::IsRMBPressed(), Input::WasLMBPressed(), Input::WasRMBPressed());
    CPSCounter::UpdateAnimation(now);

    ImGui::NewFrame();

    if (MotionBlur::g_motionBlurEnabled && !g_showMenu && MotionBlur::g_previousFrames.size() > 0 && MotionBlur::g_motionBlurAnim > 0.01f) {
        float currentTime = (float)GetTickCount64() / 1000.0f;
        ImVec2 screenSize = ImGui::GetIO().DisplaySize;
        ImDrawList* blurDraw = ImGui::GetBackgroundDrawList();
    
        if (MotionBlur::g_blurType == "Average Pixel Blur") {
            float alpha = 0.25f;
            float bleedFactor = 0.95f;
            for (const auto& frame : MotionBlur::g_previousFrames) {
                if (frame) {
                    ImU32 col = IM_COL32(255, 255, 255, (int)(alpha * MotionBlur::g_motionBlurAnim * 255.0f));
                    blurDraw->AddImage((ImTextureID)frame, ImVec2(0, 0), screenSize, ImVec2(0, 0), ImVec2(1, 1), col);
                    alpha *= bleedFactor;
                }
            }
        } 
        else if (MotionBlur::g_blurType == "Ghost Frames") {
            float alpha = 0.30f;
            float bleedFactor = 0.80f;
            for (const auto& frame : MotionBlur::g_previousFrames) {
                if (frame) {
                    ImU32 col = IM_COL32(255, 255, 255, (int)(alpha * MotionBlur::g_motionBlurAnim * 255.0f));
                    blurDraw->AddImage((ImTextureID)frame, ImVec2(0, 0), screenSize, ImVec2(0, 0), ImVec2(1, 1), col);
                    alpha *= bleedFactor;
                }
            }
        }
        else if (MotionBlur::g_blurType == "Time Aware Blur") {
            float T = MotionBlur::g_blurTimeConstant;
            std::vector<float> weights;
            float totalWeight = 0.0f;
            
            for (size_t i = 0; i < MotionBlur::g_previousFrames.size(); i++) {
                float age = currentTime - MotionBlur::g_frameTimestamps[i];
                float weight = expf(-age / T);
                weights.push_back(weight);
                totalWeight += weight;
            }
            
            if (totalWeight > 0.0f) {
                for (float& w : weights) {
                    w /= totalWeight;
                }
            }
            
            for (size_t i = 0; i < MotionBlur::g_previousFrames.size(); i++) {
                if (MotionBlur::g_previousFrames[i] && weights[i] > 0.001f) {
                    ImU32 col = IM_COL32(255, 255, 255, (int)(weights[i] * MotionBlur::g_motionBlurAnim * 255.0f));
                    blurDraw->AddImage((ImTextureID)MotionBlur::g_previousFrames[i], ImVec2(0, 0), screenSize, ImVec2(0, 0), ImVec2(1, 1), col);
                }
            }
        }
        else if (MotionBlur::g_blurType == "Real Motion Blur") {
            float alpha = 0.35f;
            float bleedFactor = 0.85f;
            for (const auto& frame : MotionBlur::g_previousFrames) {
                if (frame) {
                    ImU32 col = IM_COL32(255, 255, 255, (int)(alpha * MotionBlur::g_motionBlurAnim * 255.0f));
                    blurDraw->AddImage((ImTextureID)frame, ImVec2(0, 0), screenSize, ImVec2(0, 0), ImVec2(1, 1), col);
                    alpha *= bleedFactor;
                }
            }
        }
        else if (MotionBlur::g_blurType == "V4") {
            float alpha = 0.35f;
            float bleedFactor = 0.85f;
            for (const auto& frame : MotionBlur::g_previousFrames) {
                if (frame) {
                    ImU32 col = IM_COL32(255, 255, 255, (int)(alpha * MotionBlur::g_motionBlurAnim * 255.0f));
                    blurDraw->AddImage((ImTextureID)frame, ImVec2(0, 0), screenSize, ImVec2(0, 0), ImVec2(1, 1), col);
                    alpha *= bleedFactor;
                }
            }
        }
    }

    g_arrayListHud.HandleDrag(g_showMenu);
    g_arrayListHud.ClampToScreen();
    if (g_arrayListHud.pos.x == 0 && g_arrayListHud.pos.y == 10) {
        g_arrayListHud.pos.x = sw - 250;
    }
    ArrayList::g_hud = &g_arrayListHud;

    GUI::RenderNotification(sw, sh);
    if (GUI::g_menuAnim > 0.001f) {
        GUI::RenderMenu(sw, sh);
    }
    
    Module::RenderDisplay(sw, sh);

    ImGui::Render();
    
    pContext->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    UnlockFPS::UpdateFPS();
    
    HRESULT hr = Hook::oPresent(pSwapChain, SyncInterval, Flags);
    if (FAILED(hr) && (Flags & 0x0200)) {
        // Fallback: Try without ALLOW_TEARING if it failed
        Flags &= ~0x0200;
        hr = Hook::oPresent(pSwapChain, SyncInterval, Flags);
    }
    return hr;
}

DWORD WINAPI MainThread(LPVOID lpReserved) {
    UnlockFPS::Initialize();
    UnlockFPS::SetFPS(60.0f);
    Hook::Initialize();
    return 0;
}

BOOL WINAPI DllMain(HMODULE hMod, DWORD dwReason, LPVOID lpReserved) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        g_hModule = hMod;
        DisableThreadLibraryCalls(hMod);
        CreateThread(NULL, 0, MainThread, NULL, 0, NULL);
    }
    return TRUE;
}
