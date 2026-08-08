/*
Under an4rch Development Public Source License 1.0
*/

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000000
#endif

#include "UnlockFPS.hpp"
#include "Animations/Animations.hpp"
#include "ImGui/imgui.h"
#include "GUI/GUI.hpp"
#include "../../Globals.hpp"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <mmsystem.h>
#include <cstdio>
#include <cmath>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "winmm.lib")

// Static member initialization
bool UnlockFPS::g_unlockFpsEnabled = false;
float UnlockFPS::g_fpsLimit = 0.0f;
bool UnlockFPS::g_lowLatency = true;
ULONGLONG UnlockFPS::g_unlockFpsEnableTime = 0;
ULONGLONG UnlockFPS::g_unlockFpsDisableTime = 0;
HANDLE UnlockFPS::g_waitableObject = NULL;
ID3D11Device* UnlockFPS::g_device = nullptr;
IDXGISwapChain3* UnlockFPS::g_swapChain3 = nullptr;
IDXGISwapChain* UnlockFPS::g_cachedChain = nullptr;
UINT UnlockFPS::g_originalLatency = 2;
bool UnlockFPS::g_originalLatencyValid = false;

// Internal state
static float targetFPS = 0.0f;
static LARGE_INTEGER perfFrequency = { 0 };
static LONGLONG frameDurationTicks = 0;
static LARGE_INTEGER lastPresentTime = { 0 };
static bool isFirstFrame = true;
static bool wasEnabled = false;
static float lastSetFPS = 0.0f;
static bool timerHighRes = false;

// Constants for animation
#define FADE_IN_TIME 0.3f
#define FADE_OUT_TIME 0.3f
#define SLIDE_TIME 0.4f

void UnlockFPS::Initialize() {
    // Allow the timer to sleep in ~1ms increments for accurate frame pacing
    if (!timerHighRes) {
        timerHighRes = (timeBeginPeriod(1) == TIMERR_NOERROR);
    }
    if (perfFrequency.QuadPart == 0) {
        QueryPerformanceFrequency(&perfFrequency);
        if (perfFrequency.QuadPart == 0) {
            perfFrequency.QuadPart = 1;
        }
    }
    frameDurationTicks = 0;
    targetFPS = 0.0f;
}

void UnlockFPS::OnDeviceReady(ID3D11Device* device) {
    g_device = device;
    if (!device) return;

    IDXGIDevice1* dxgiDevice = nullptr;
    if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice1), (void**)&dxgiDevice))) {
        UINT latency = 0;
        if (SUCCEEDED(dxgiDevice->GetMaximumFrameLatency(&latency)) && latency != 0) {
            g_originalLatency = latency;
            g_originalLatencyValid = true;
        }
        dxgiDevice->Release();
    }
}

void UnlockFPS::AcquireSwapChain(IDXGISwapChain* pSwapChain) {
    if (g_swapChain3) {
        g_swapChain3->Release();
        g_swapChain3 = nullptr;
    }
    if (g_waitableObject) {
        CloseHandle(g_waitableObject);
        g_waitableObject = NULL;
    }
    if (!pSwapChain) return;

    // DXGI 1.3+ waitable swapchain: we can force frame latency 0.
    IDXGISwapChain3* sc3 = nullptr;
    if (SUCCEEDED(pSwapChain->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&sc3))) {
        g_swapChain3 = sc3;
        g_waitableObject = sc3->GetFrameLatencyWaitableObject();
        return;
    }
    // DXGI 1.2 waitable swapchain: no latency setter, but the waitable object
    // handle lets us bypass the game's frame-pacing wait (see WaitForSingleObject hook).
    IDXGISwapChain2* sc2 = nullptr;
    if (SUCCEEDED(pSwapChain->QueryInterface(__uuidof(IDXGISwapChain2), (void**)&sc2))) {
        g_waitableObject = sc2->GetFrameLatencyWaitableObject();
        sc2->Release();
    }
}

void UnlockFPS::ApplyLatency(bool low) {
    if (!g_device) return;

    IDXGIDevice1* dxgiDevice = nullptr;
    if (SUCCEEDED(g_device->QueryInterface(__uuidof(IDXGIDevice1), (void**)&dxgiDevice))) {
        UINT target = low ? 1 : (g_originalLatencyValid ? g_originalLatency : 3);
        dxgiDevice->SetMaximumFrameLatency(target);
        dxgiDevice->Release();
    }
}

void UnlockFPS::UpdateFPS(IDXGISwapChain* pSwapChain) {
    if (g_unlockFpsEnabled) {
        if (!wasEnabled) {
            if (perfFrequency.QuadPart == 0) {
                Initialize();
            }
            SetFPS(g_fpsLimit);
            lastSetFPS = g_fpsLimit;
            g_cachedChain = nullptr;
            wasEnabled = true;
        } else if (lastSetFPS != g_fpsLimit) {
            SetFPS(g_fpsLimit);
            lastSetFPS = g_fpsLimit;
        }

        // Re-acquire the swapchain if it was recreated (device reset / fullscreen change)
        if (pSwapChain != g_cachedChain) {
            g_cachedChain = pSwapChain;
            AcquireSwapChain(pSwapChain);
        }

        // Waitable swapchains (DXGI 1.3, used by UWP): latency 0 makes the frame
        // latency waitable object signal immediately, removing the internal
        // refresh-rate pacing that keeps FPS stuck at the monitor refresh even
        // with vsync off. Non-waitable swapchains reject this call; ignore it.
        if (g_swapChain3) {
            g_swapChain3->SetMaximumFrameLatency(0);
        }
        // The frame latency waitable object is an auto-reset event the game's
        // render thread blocks on. Signaling it every frame un-paces the loop:
        // the game never waits for the compositor, so FPS stops being capped at
        // the refresh rate. Works regardless of which handle duplicate the game
        // waits on, since SetEvent acts on the underlying kernel object.
        if (g_waitableObject) {
            SetEvent(g_waitableObject);
        }
        // Non-waitable fallback: keep the device frame queue at 1 to cut lag.
        ApplyLatency(g_lowLatency);
    } else {
        if (wasEnabled) {
            if (g_swapChain3) {
                UINT restore = g_originalLatencyValid ? g_originalLatency : 2;
                g_swapChain3->SetMaximumFrameLatency(restore);
                g_swapChain3->Release();
            }
            if (g_waitableObject) {
                CloseHandle(g_waitableObject);
                g_waitableObject = NULL;
            }
            ApplyLatency(false);
            g_swapChain3 = nullptr;
            g_cachedChain = nullptr;
            wasEnabled = false;
        }
        isFirstFrame = true;
        lastPresentTime.QuadPart = 0;
        return;
    }

    // No cap selected: real unlock, Present already runs vsync-off without
    // burning a core in the busy-wait loop.
    if (frameDurationTicks <= 0 || targetFPS <= 0.0f) {
        return;
    }

    // Software frame pacing (only applied when a limit is set)
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    if (isFirstFrame || lastPresentTime.QuadPart == 0) {
        lastPresentTime = now;
        isFirstFrame = false;
        return;
    }

    LONGLONG elapsed = now.QuadPart - lastPresentTime.QuadPart;
    if (elapsed < frameDurationTicks) {
        LONGLONG remaining = frameDurationTicks - elapsed;
        DWORD sleepMs = (DWORD)((remaining * 1000) / perfFrequency.QuadPart);
        if (sleepMs > 2) {
            Sleep(sleepMs - 2);
        }
        do {
            QueryPerformanceCounter(&now);
            elapsed = now.QuadPart - lastPresentTime.QuadPart;
        } while (elapsed < frameDurationTicks);
    }

    lastPresentTime = now;
}

void UnlockFPS::PreparePresent(UINT& SyncInterval, UINT& Flags) {
    if (g_unlockFpsEnabled) {
        SyncInterval = 0;
        Flags |= 0x0200; // DXGI_PRESENT_ALLOW_TEARING
    } else if (!g_vsync) {
        SyncInterval = 0;
    }
}

void UnlockFPS::SetFPS(float fps)
{
    if (fps < 0.0f) fps = 0.0f;
    if (fps > 1000.0f) fps = 1000.0f;

    targetFPS = fps;
    if (perfFrequency.QuadPart == 0) {
        QueryPerformanceFrequency(&perfFrequency);
        if (perfFrequency.QuadPart == 0) {
            perfFrequency.QuadPart = 1;
        }
    }

    frameDurationTicks = (fps > 0.0f)
        ? (LONGLONG)((double)perfFrequency.QuadPart / targetFPS)
        : 0;
}

float UnlockFPS::GetFPS()
{
    return targetFPS;
}

void UnlockFPS::RenderArrayList(ImDrawList* draw, ImVec2 arrayListStart, float& yPos, ImVec2& arrayListEnd) {
    if (g_unlockFpsEnabled || g_unlockFpsDisableTime > 0) {
        ULONGLONG now = GetTickCount64();
        float timeSinceEnable = (float)(now - g_unlockFpsEnableTime) / 1000.0f;
        float timeSinceDisable = (float)(now - g_unlockFpsDisableTime) / 1000.0f;

        float unlockFpsAlpha = 255.0f;
        float slideOffset = 0.0f;

        if (g_unlockFpsEnabled) {
            unlockFpsAlpha = Animations::SmoothInertia(fminf(1.0f, timeSinceEnable / FADE_IN_TIME)) * 255.0f;
            float slideProgress = fminf(1.0f, timeSinceEnable / SLIDE_TIME);
            slideOffset = Animations::SmoothInertia(slideProgress) * 60.0f - 60.0f;
        } else if (timeSinceDisable < FADE_OUT_TIME) {
            unlockFpsAlpha = Animations::SmoothInertia(1.0f - (timeSinceDisable / FADE_OUT_TIME)) * 255.0f;
        } else {
            g_unlockFpsDisableTime = 0;
        }

        if (unlockFpsAlpha > 1.0f && draw) {
            char uBuf[64];
            if (g_fpsLimit <= 0.0f) {
                sprintf_s(uBuf, sizeof(uBuf), "Unlock FPS - Unlimited");
            } else {
                sprintf_s(uBuf, sizeof(uBuf), "Unlock FPS - %.0f", g_fpsLimit);
            }
            ImVec2 textSize = ImGui::CalcTextSize(uBuf);
            float xPosU = arrayListStart.x + 300.0f - textSize.x - 10.0f;

            draw->AddText(ImVec2(xPosU + slideOffset - 1, yPos + 1), IM_COL32(0, 0, 0, 220), uBuf);
            draw->AddText(ImVec2(xPosU + slideOffset, yPos), IM_COL32(100, 255, 100, (int)unlockFpsAlpha), uBuf);
            yPos += 18.0f;
            arrayListEnd.y = yPos;
        }
    }
}

void UnlockFPS::RenderMenu() {
    bool prev = g_unlockFpsEnabled;
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
        "Forces Vsync off + allows tearing for a real FPS unlock on UWP.");
    GUI::RenderCustomSwitch("Unlock FPS", &g_unlockFpsEnabled);
    if (prev != g_unlockFpsEnabled) {
        if (g_unlockFpsEnabled) {
            g_unlockFpsEnableTime = GetTickCount64();
            g_unlockFpsDisableTime = 0;
        } else {
            g_unlockFpsDisableTime = GetTickCount64();
            g_unlockFpsEnableTime = 0;
        }
    }

    if (GUI::BeginModuleSettings("UnlockFPS", &g_unlockFpsEnabled)) {
        ImGui::SliderFloat("FPS Limit", &g_fpsLimit, 0.0f, 1000.0f,
            g_fpsLimit <= 0.0f ? "Unlimited" : "%.0f FPS");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("0 = no software cap (recommended). Set a limit only to reduce heat or stabilize frame times.");
        }

        GUI::RenderCustomSwitch("Low Latency (Frame Queue 1)", &g_lowLatency);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Cuts the swapchain frame queue to 1 to reduce input lag at high FPS.");
        }
        GUI::EndModuleSettings();
    }
}
