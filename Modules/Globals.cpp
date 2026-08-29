/*
Under an4rch Development Public Source License 1.0
*/

#include "Globals.hpp"

// Global variable definitions
ImGuiConfigFlags g_imguiConfigFlags = ImGuiConfigFlags_None;
WNDPROC oWndProc = NULL;
HMODULE g_hModule = NULL;

ID3D11Device* pDevice = NULL;
ID3D11DeviceContext* pContext = NULL;
ID3D11RenderTargetView* mainRenderTargetView = NULL;
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

HudElement g_watermarkHud = { ImVec2(10, 10), ImVec2(400, 80) };
HudElement g_renderInfoHud = { ImVec2(10, 100), ImVec2(220, 120) };
HudElement g_arrayListHud = { ImVec2(0, 10), ImVec2(300, 400) };
HudElement g_keystrokesHud = { ImVec2(30, 0), ImVec2(140, 150) };
HudElement g_cpsHud = { ImVec2(500, 400), ImVec2(80, 30) };
HudElement g_fpsOverlayHud = { ImVec2(10, 250), ImVec2(100, 35) };
HudElement g_pingHud = { ImVec2(0, 0), ImVec2(130, 28) };
HudElement g_playerInfoHud = { ImVec2(10, 330), ImVec2(260, 64) };
HudElement g_mouseStrokesHud = { ImVec2(10, 420), ImVec2(75, 75) };
