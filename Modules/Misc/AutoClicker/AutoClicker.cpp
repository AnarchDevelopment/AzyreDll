/*
Under an4rch Development Public Source License 1.0
*/

#include "AutoClicker.hpp"
#include "Animations/Animations.hpp"
#include "ImGui/imgui.h"
#include "GUI/GUI.hpp"
#include <windows.h>
#include <cstdlib>
#include <cmath>

// ─── Static member initialization ───────────────────────────────────────────
bool  AutoClicker::g_enabled       = false;
bool  AutoClicker::g_rightClick    = false;
float AutoClicker::g_cps           = 12.0f;
float AutoClicker::g_randomRange   = 2.0f;
bool  AutoClicker::g_holdMode      = true;

ULONGLONG AutoClicker::g_enableTime  = 0;
ULONGLONG AutoClicker::g_disableTime = 0;

// Internal state
static ULONGLONG s_lastClickTime = 0;
static float     s_nextInterval  = 0.0f;

// Game window handle — defined in dllmain.cpp
extern HWND g_window;

static float RandomFloat(float lo, float hi) {
    return lo + ((float)rand() / (float)RAND_MAX) * (hi - lo);
}

static float CalcNextInterval(float cps, float range) {
    float base = 1000.0f / cps;
    float var  = (range > 0.0f) ? RandomFloat(-base * 0.15f, base * 0.15f) : 0.0f;
    float ms   = base + var;
    if (ms < 20.0f)   ms = 20.0f;
    if (ms > 2000.0f) ms = 2000.0f;
    return ms;
}

// ─── Click via PostMessage ────────────────────────────────────────────────────
// PostMessage bypasses AppContainer input injection restrictions because it
// posts directly to the window's own message queue from the same process.
static void DoClick(bool rightBtn) {
    HWND hwnd = g_window;
    if (!hwnd) {
        // Fallback: find the CoreWindow if g_window hasn't been set yet
        hwnd = FindWindowW(L"Windows.UI.Core.CoreWindow", nullptr);
    }
    if (!hwnd) return;

    // Get cursor position relative to the game window
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd, &pt);
    LPARAM lp = MAKELPARAM(pt.x, pt.y);

    if (rightBtn) {
        PostMessage(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, lp);
        Sleep(8);
        PostMessage(hwnd, WM_RBUTTONUP,   0,          lp);
    } else {
        PostMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
        Sleep(8);
        PostMessage(hwnd, WM_LBUTTONUP,   0,          lp);
    }
}

void AutoClicker::Initialize() {
    srand((unsigned)GetTickCount64());
    s_nextInterval = CalcNextInterval(g_cps, g_randomRange);
}

void AutoClicker::Tick() {
    if (!g_enabled) return;

    if (g_holdMode) {
        int holdKey = g_rightClick ? VK_RBUTTON : VK_LBUTTON;
        if (!(GetAsyncKeyState(holdKey) & 0x8000)) return;
    }

    ULONGLONG now = GetTickCount64();
    if (s_lastClickTime == 0) { s_lastClickTime = now; return; }

    float elapsed = (float)(now - s_lastClickTime);
    if (elapsed < s_nextInterval) return;

    DoClick(g_rightClick);

    s_lastClickTime = now;
    s_nextInterval  = CalcNextInterval(g_cps, g_randomRange);
}

void AutoClicker::RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd) {
    if (!g_enabled && g_disableTime == 0) return;

    ULONGLONG now = GetTickCount64();
    float timeSinceEnable  = (float)(now - g_enableTime)  / 1000.0f;
    float timeSinceDisable = (float)(now - g_disableTime) / 1000.0f;
    const float FADE = 0.3f;

    float alpha = 255.0f, slide = 0.0f;
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
        char buf[64];
        sprintf_s(buf, sizeof(buf), "AutoClicker - %.0f CPS", g_cps);
        ImVec2 textSize = ImGui::CalcTextSize(buf);
        float x = arrayListStart.x + 300.0f - textSize.x - 10.0f;
        draw->AddText(ImVec2(x + slide - 1, yPos + 1), IM_COL32(0,0,0,200), buf);
        draw->AddText(ImVec2(x + slide,     yPos),     IM_COL32(100,200,255,(int)alpha), buf);
        yPos += 18.0f;
        arrayListEnd.y = yPos;
    }
}

void AutoClicker::RenderMenu() {
    bool prev = g_enabled;
    GUI::RenderCustomSwitch("AutoClicker", &g_enabled);
    if (prev != g_enabled) {
        if (g_enabled) {
            g_enableTime  = GetTickCount64();
            g_disableTime = 0;
            s_lastClickTime = 0;
            s_nextInterval  = CalcNextInterval(g_cps, g_randomRange);
        } else {
            g_disableTime = GetTickCount64();
            g_enableTime  = 0;
        }
    }

    if (GUI::BeginModuleSettings("AutoClicker", &g_enabled)) {
        GUI::RenderSlider("CPS##AC",          &g_cps,         1.0f, 30.0f, "%.1f");
        GUI::RenderSlider("Random Range##AC", &g_randomRange, 0.0f,  8.0f, "%.1f");
        GUI::RenderCustomSwitch("Right Click##AC", &g_rightClick);
        GUI::RenderCustomSwitch("Hold Mode##AC",   &g_holdMode);
        GUI::EndModuleSettings();
    }
}
