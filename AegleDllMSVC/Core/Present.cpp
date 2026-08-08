/*
Under an4rch Development Public Source License 1.0
*/

#include "Present.hpp"
#include "../ImGui/imgui.h"
#include "../ImGui/backend/imgui_impl_dx11.h"
#include "../Modules/ModuleHeader.hpp"
#include "../Modules/ModuleManager.hpp"
#include "../Modules/Globals.hpp"
#include "../GUI/GUI.hpp"
#include "../GUI/DX11/ImGuiRenderer.hpp"
#include "../ArrayList/ArrayList.hpp"
#include "../Hook/Hook.hpp"
#include "../Input/Input.hpp"

DWORD WINAPI Present::MainThread(LPVOID lpReserved) {
    UnlockFPS::Initialize();
    UnlockFPS::SetFPS(UnlockFPS::g_fpsLimit);
    Hook::Initialize();
    return 0;
}

HRESULT Present::Run(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    UnlockFPS::PreparePresent(SyncInterval, Flags);

    if (!pDevice) {
        ImGuiDX11::Initialize(pSwapChain);
    }

    float sw = 0, sh = 0;
    ImGuiDX11::PrepareFrame(pSwapChain, sw, sh);
    MotionBlur::OnPresent(pDevice, pContext, pSwapChain);

    if (sw <= 0) return Hook::Present(pSwapChain, 0, Flags);

    GUI::HandleMenuToggle();
    GUI::SyncMenuState();

    ULONGLONG now = GetTickCount64();
    float dt = (float)(now - g_lastTime) / 1000.0f;
    g_lastTime = now;
    GUI::UpdateAnimation(now, dt);
    Module::UpdateAnimation(now);

    ImGui_ImplDX11_NewFrame();
    // Cursor handling. The menu uses ImGui's software cursor: when the OS cursor
    // is hidden (game cursor in-world), tell ImGui to draw its own cursor.
    Input::Update(g_window, sw, sh, g_showMenu, g_showMenu && !Input::IsWindowsCursorVisible());

    CPSCounter::UpdateCPS(now, Input::IsLMBPressed(), Input::IsRMBPressed(), Input::WasLMBPressed(), Input::WasRMBPressed());
    CPSCounter::UpdateAnimation(now);

    ImGui::NewFrame();

    MotionBlur::RenderTrail(g_showMenu);

    ArrayList::HandleHudDrag(sw, GUI::IsHudEditable());

    GUI::RenderNotification(sw, sh);
    GUI::RenderMenu(sw, sh);

    Module::RenderDisplay(sw, sh);

    GUI::RenderBackdrop(pDevice, pContext, pSwapChain, sw, sh);

    ImGuiDX11::RenderFrame();

    // Capture screenshot if pending
    Screenshot::TryCaptureFrame(pDevice, pContext, pSwapChain);

    UnlockFPS::UpdateFPS(pSwapChain);

    return Hook::Present(pSwapChain, SyncInterval, Flags);
}
