/*
Under an4rch Development Public Source License 1.0
*/

#include "AntiAFK.hpp"
#include "Animations/Animations.hpp"
#include "ImGui/imgui.h"
#include "GUI/GUI.hpp"
#include <windows.h>
#include <cstdlib>
#include <cmath>

// ─── Static member initialization ───────────────────────────────────────────
bool  AntiAFK::g_enabled         = false;
float AntiAFK::g_intervalSecs    = 30.0f;
float AntiAFK::g_pressDurationMs = 150.0f;
bool  AntiAFK::g_randomizeKeys   = true;
bool  AntiAFK::g_jump            = false;

ULONGLONG AntiAFK::g_enableTime  = 0;
ULONGLONG AntiAFK::g_disableTime = 0;

// Internal state
static ULONGLONG s_lastActionTime = 0;
static bool      s_isPressingKey  = false;
static ULONGLONG s_pressStartTime = 0;
static WORD      s_currentKey     = 0;

// Game window handle — defined in dllmain.cpp
extern HWND g_window;

// ─── WASD keys ───────────────────────────────────────────────────────────────
static const WORD WASD_KEYS[] = { 'W', 'A', 'S', 'D' };

// PostMessage key press/release — works inside UWP AppContainer without
// the inputInjectionBrokered capability that SendInput/WinRT require.
static HWND GetGameHwnd() {
    if (g_window) return g_window;
    return FindWindowW(L"Windows.UI.Core.CoreWindow", nullptr);
}

static void PressKey(WORD vk) {
    HWND hwnd = GetGameHwnd();
    if (!hwnd) return;
    // LPARAM for WM_KEYDOWN: scan code in bits 16-23, repeat count in bits 0-15
    LPARAM lp = (LPARAM)(MapVirtualKey(vk, MAPVK_VK_TO_VSC) << 16) | 1;
    PostMessage(hwnd, WM_KEYDOWN, (WPARAM)vk, lp);
}

static void ReleaseKey(WORD vk) {
    HWND hwnd = GetGameHwnd();
    if (!hwnd) return;
    // LPARAM for WM_KEYUP: transition bit (bit 31), previous state (bit 30), scan code
    UINT scan = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
    LPARAM lp = (LPARAM)((scan << 16) | (1 << 30) | (1 << 31) | 1);
    PostMessage(hwnd, WM_KEYUP, (WPARAM)vk, lp);
}

void AntiAFK::Initialize() {
    srand((unsigned)GetTickCount64());
}

void AntiAFK::Tick() {
    if (!g_enabled) {
        if (s_isPressingKey && s_currentKey != 0) {
            ReleaseKey(s_currentKey);
            s_isPressingKey = false;
            s_currentKey    = 0;
        }
        return;
    }

    ULONGLONG now = GetTickCount64();

    if (s_isPressingKey) {
        float elapsed = (float)(now - s_pressStartTime);
        if (elapsed >= g_pressDurationMs) {
            ReleaseKey(s_currentKey);
            s_isPressingKey = false;
            s_currentKey    = 0;
        }
        return;
    }

    if (s_lastActionTime == 0) { s_lastActionTime = now; return; }

    float elapsed = (float)(now - s_lastActionTime) / 1000.0f;
    if (elapsed < g_intervalSecs) return;

    s_currentKey = g_randomizeKeys ? WASD_KEYS[rand() % 4] : (WORD)'W';

    PressKey(s_currentKey);
    s_isPressingKey  = true;
    s_pressStartTime = now;
    s_lastActionTime = now;

    if (g_jump) {
        HWND hwnd = GetGameHwnd();
        if (hwnd) {
            UINT scan = MapVirtualKey(VK_SPACE, MAPVK_VK_TO_VSC);
            LPARAM lpDown = (LPARAM)((scan << 16) | 1);
            LPARAM lpUp   = (LPARAM)((scan << 16) | (1 << 30) | (1 << 31) | 1);
            PostMessage(hwnd, WM_KEYDOWN, VK_SPACE, lpDown);
            Sleep(80);
            PostMessage(hwnd, WM_KEYUP,   VK_SPACE, lpUp);
        }
    }
}

void AntiAFK::RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd) {
    if (!g_enabled && g_disableTime == 0) return;

    ULONGLONG now = GetTickCount64();
    float timeSinceEnable  = (float)(now - g_enableTime)  / 1000.0f;
    float timeSinceDisable = (float)(now - g_disableTime) / 1000.0f;
    const float FADE = 0.3f;

    float alpha = 255.0f;
    float slide = 0.0f;
    if (g_enabled) {
        alpha = Animations::SmoothInertia(fminf(1.0f, timeSinceEnable / FADE)) * 255.0f;
        slide = Animations::SmoothInertia(fminf(1.0f, timeSinceEnable / 0.4f)) * 60.0f - 60.0f;
    } else if (timeSinceDisable < FADE) {
        alpha = Animations::SmoothInertia(1.0f - timeSinceDisable / FADE) * 255.0f;
    } else {
        g_disableTime = 0;
        return;
    }

    if (alpha > 1.0f && draw) {
        const char* label = "Anti-AFK";
        ImVec2 textSize   = ImGui::CalcTextSize(label);
        float x = arrayListStart.x + 300.0f - textSize.x - 10.0f;
        draw->AddText(ImVec2(x + slide - 1, yPos + 1), IM_COL32(0,0,0,200), label);
        draw->AddText(ImVec2(x + slide,     yPos),     IM_COL32(255,200,80,(int)alpha), label);
        yPos += 18.0f;
        arrayListEnd.y = yPos;
    }
}

void AntiAFK::RenderMenu() {
    bool prev = g_enabled;
    GUI::RenderCustomSwitch("Anti-AFK", &g_enabled);
    if (prev != g_enabled) {
        if (g_enabled) {
            g_enableTime    = GetTickCount64();
            g_disableTime   = 0;
            s_lastActionTime = 0;
        } else {
            g_disableTime = GetTickCount64();
            g_enableTime  = 0;
        }
    }

    if (GUI::BeginModuleSettings("AntiAFK", &g_enabled)) {
        GUI::RenderSlider("Intervalo (s)##AFK",  &g_intervalSecs,    5.0f, 120.0f, "%.0f s");
        GUI::RenderSlider("Duracion (ms)##AFK",  &g_pressDurationMs, 50.0f, 500.0f, "%.0f ms");
        GUI::RenderCustomSwitch("Aleatorizar Teclas##AFK", &g_randomizeKeys);
        GUI::RenderCustomSwitch("Saltar##AFK",             &g_jump);
        GUI::EndModuleSettings();
    }
}
