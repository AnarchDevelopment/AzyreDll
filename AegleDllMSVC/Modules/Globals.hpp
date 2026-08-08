/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <windows.h>
#include <d3d11.h>
#include <stdint.h>
#include "../ImGui/imgui.h"
#include "../Utils/HudElement.hpp"

// Global variables extern declarations
extern ImGuiConfigFlags g_imguiConfigFlags;
extern WNDPROC oWndProc;
extern HMODULE g_hModule;
extern ID3D11Device* pDevice;
extern ID3D11DeviceContext* pContext;
extern ID3D11RenderTargetView* mainRenderTargetView;
extern HWND g_window;

extern bool g_showMenu;
extern bool g_RequestUnload;
extern bool g_wasInWorld;
extern float g_menuAnim;
extern ULONGLONG g_lastTime;
extern ULONGLONG g_lastToggle;
extern ULONGLONG g_notifStart;
extern bool g_vsync;
extern float g_lastW;
extern float g_lastH;

// Tab animation
extern int g_currentTab;
extern int g_previousTab;
extern ULONGLONG g_tabChangeTime;
extern float g_tabAnim;
extern bool g_firstTabOpen;
extern uintptr_t g_gameBase;

// HUD elements
extern HudElement g_watermarkHud;
extern HudElement g_renderInfoHud;
extern HudElement g_arrayListHud;
extern HudElement g_keystrokesHud;
extern HudElement g_cpsHud;
extern HudElement g_fpsOverlayHud;
extern HudElement g_pingHud;
extern HudElement g_playerInfoHud;
