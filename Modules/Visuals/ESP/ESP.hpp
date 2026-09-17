/*
Under an4rch Development Public Source License 1.0
- ESP (2D world-to-screen). Same flow as the working ESP.py:
  background thread only caches addresses (~1s rescan), render reads live.
*/

#pragma once

#include <cstdint>
#include <windows.h>
#include <vector>
#include <mutex>
#include <atomic>
#include <string>

struct ImDrawList;
struct ImVec2;
struct HudElement;

class ESP {
public:
    static bool g_enabled;

    // Elements
    static bool g_showBox;
    static bool g_showName;
    static bool g_showDistance;
    static bool g_showHealth;
    static bool g_showTracer;

    // Settings
    static float g_maxDistance;
    static float g_fov;         // fallback FOV if the game FOV can't be read
    static float g_eyeHeight;   // camera eye height above player pos
    static float g_boxThickness;
    static uintptr_t g_fovAddr; // absolute address of the game FOV float

    // Colors
    static float g_boxColor[4];
    static float g_nameColor[4];
    static float g_tracerColor[4];
    static float g_distanceColor[4];

    static void Initialize(uintptr_t gameBase);
    static void Enable();
    static void Disable();
    static void Tick();
    static void RenderDisplay(float screenWidth, float screenHeight);
    static void RenderMenu();
    static bool IsEnabled() { return g_enabled; }

    // Public for GetCameraData (free function)
    static void GetCameraData(uintptr_t lp, float& cx, float& cy, float& cz, float& camYaw, float& camPitch);
    static bool ReadPos(uintptr_t addr, float* x, float* y, float* z);
    static bool ReadAng(uintptr_t addr, float* yaw, float* pitch);
    static void CacheThreadFn();

private:
    static uintptr_t g_baseAddress;
    static std::atomic<uintptr_t> g_lp;
    static std::atomic<uintptr_t> g_mc;   // MinecraftClient*
    static std::atomic<uintptr_t> g_lr;   // LevelRenderer*
    static std::vector<uintptr_t> g_rpCache;
    static std::mutex g_rpMtx;
    static ULONGLONG g_lastScan;
    static HANDLE g_cacheThread;
    static volatile bool g_cacheRunning;
    static std::string g_myName;

    static constexpr uintptr_t VTABLE_LOCALPLAYER      = 0xE63B88;
    static constexpr uintptr_t VTABLE_MINECRAFTCLIENT  = 0xE44B28;
    static constexpr uintptr_t VTABLE_REMOTEPLAYER     = 0xE645A0;
    static constexpr uintptr_t VTABLE_REMOTEPLAYER_ALT = 0xE64588;
    static constexpr uintptr_t OFFSET_LP_TO_MC  = 0x1020;
    static constexpr uintptr_t OFFSET_MC_TO_LR  = 0xF0;
    static constexpr uintptr_t OFFSET_CAM_VEC   = 0x2548;
    static constexpr uintptr_t OFFSET_PITCH   = 0x3C;
    static constexpr uintptr_t OFFSET_YAW     = 0x40;
    static constexpr uintptr_t OFFSET_POS_X   = 0xB4;
    static constexpr uintptr_t OFFSET_POS_Y   = 0xB8;
    static constexpr uintptr_t OFFSET_POS_Z   = 0xBC;
    static constexpr uintptr_t OFFSET_HEIGHT  = 0xD4;
    static constexpr uintptr_t OFFSET_HEALTH  = 0x220;
    static constexpr uintptr_t OFFSET_NAME    = 0x0E10;

    static void StartCacheThread();

    static void FindLocalPlayer();
    static void RefreshRemotePlayers();

    static bool ReadName(uintptr_t addr, char* out, size_t maxLen);
    static bool ReadHeight(uintptr_t addr, float* h);
    static bool ReadHealth(uintptr_t addr, int* hp);
    static float ReadFov();
    static bool IsAlive(uintptr_t addr);
    static bool IsValidPos(float x, float y, float z);
};