/*
Under an4rch Development Public Source License 1.0
- AimAssist: scan runs on a native CreateThread (like Timer.cpp).
  Tick() only reads cached data (no heavy work, no crashes).
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

class AimAssist {
public:
    static bool g_enabled;
    static float g_smoothness;
    static float g_maxDistance;
    static float g_fov;
    static bool g_targetHead;
    static bool g_aimYawPitch;
    static bool g_crosshairPriority;

    static uintptr_t g_targetAddr;
    static float g_targetDist;
    static int g_enemiesCount;
    static int g_cachedRpCount;
    static ULONGLONG g_enableTime;
    static ULONGLONG g_disableTime;
    static std::string g_myName;

    static void Initialize(uintptr_t gameBase);
    static void Enable();
    static void Disable();
    static void Tick();
    static void RenderMenu();
    static bool IsEnabled() { return g_enabled; }

private:
    static uintptr_t g_baseAddress;
    static std::atomic<uintptr_t> g_lp;
    static std::vector<uintptr_t> g_rpCache;
    static std::mutex g_rpMtx;
    static HANDLE g_cacheThread;
    static volatile bool g_cacheRunning;
    static int g_lastLPMatches;
    static int g_lastLPRegions;
    static void CacheThreadFn();
    static void StartCacheThread();

    static constexpr uintptr_t VTABLE_LOCALPLAYER      = 0xE63B88;
    static constexpr uintptr_t VTABLE_REMOTEPLAYER     = 0xE645A0;
    static constexpr uintptr_t VTABLE_REMOTEPLAYER_ALT = 0xE64588;
    static constexpr uintptr_t OFFSET_PITCH = 0x3C;
    static constexpr uintptr_t OFFSET_YAW   = 0x40;
    static constexpr uintptr_t OFFSET_POS_X = 0xB4;
    static constexpr uintptr_t OFFSET_POS_Y = 0xB8;
    static constexpr uintptr_t OFFSET_POS_Z = 0xBC;
    static constexpr uintptr_t OFFSET_HEIGHT = 0xD4;
    static constexpr uintptr_t OFFSET_HEALTH = 0x220;
    static constexpr uintptr_t OFFSET_NAME  = 0x0E10;
    static constexpr uintptr_t OFFSET_LP_MC_REF = 0x1020;

    static void FindLocalPlayer();
    static void RefreshRemotePlayers();
    static bool ReadPos(uintptr_t addr, float* x, float* y, float* z);
    static bool ReadAng(uintptr_t addr, float* yaw, float* pitch);
    static bool ReadName(uintptr_t addr, char* out, size_t maxLen);
    static bool IsAlive(uintptr_t addr);
    static void WriteAng(uintptr_t addr, float yaw, float pitch);
    static float ShortestDelta(float current, float target);
    static bool IsValidPos(float x, float y, float z);
};