/*
Under an4rch Development Public Source License 1.0
*/

#pragma once

#include <windows.h>
#include <string>
#include "imgui.h"

// Forward declarations
struct ImDrawList;
struct ImVec2;
struct HudElement;

/// @brief Ping Counter module - Real UDP (RakNet) latency counter.
///        Captures the connected server's UDP endpoint via ws2_32 hooks and
///        measures the round-trip time with native RakNet UNCONNECTED_PING /
///        UNCONNECTED_PONG packets. No TCP involved.
class PingCounter {
public:
    static bool g_showPingCounter;
    static HudElement* g_pingHud;
    
    // Animation state
    static float g_pingAnim;
    static ULONGLONG g_pingEnableTime;
    static ULONGLONG g_pingDisableTime;

    // Config options
    static float g_pingTextScale;
    static bool g_showBackground;
    static float g_bgOpacity;
    static ImVec4 g_pingTextColor;
    static ImVec4 g_pingCounterShadowColor;
    static bool g_pingTextShadow;
    static std::string g_fontName;

    // Real UDP ping state
    static int g_currentPing;            // measured RTT in ms; -1 = unknown / timeout
    static ULONGLONG g_lastPingUpdate;
    static int g_pingUpdateInterval;     // ms between pings
    static bool g_serverKnown;           // a UDP server endpoint has been captured
    static char g_serverIP[64];          // connected server address (IPv4/IPv6 string)
    static unsigned short g_serverPort;  // connected server UDP (RakNet) port

    // Manual DNS override: force-pings a hostname:port even through local proxies
    static std::string g_manualHost;
    static int g_manualPort;

    static void Initialize(HudElement* hud);
    static void UpdateAnimation(ULONGLONG now);
    static void UpdatePing(ULONGLONG now);
    static void RenderDisplay(float sw, float sh);
    static void RenderMenu();
    static void Shutdown();
};
