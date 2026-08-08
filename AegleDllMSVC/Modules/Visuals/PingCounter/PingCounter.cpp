/*
Under an4rch Development Public Source License 1.0
*/

#include <winsock2.h>
#include <ws2tcpip.h>

#include "PingCounter.hpp"
#include "../../../Utils/HudElement.hpp"
#include "../../../Animations/Animations.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../GUI/GUI.hpp"
#include "../../../minhook/MinHook.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>

#pragma warning(disable : 6387)
#pragma comment(lib, "ws2_32.lib")

bool PingCounter::g_showPingCounter = false;
HudElement* PingCounter::g_pingHud = nullptr;

float PingCounter::g_pingAnim = 0.0f;
ULONGLONG PingCounter::g_pingEnableTime = 0;
ULONGLONG PingCounter::g_pingDisableTime = 0;

float PingCounter::g_pingTextScale = 1.0f;
bool PingCounter::g_showBackground = true;
float PingCounter::g_bgOpacity = 0.5f;
ImVec4 PingCounter::g_pingTextColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
ImVec4 PingCounter::g_pingCounterShadowColor = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
bool PingCounter::g_pingTextShadow = true;
std::string PingCounter::g_fontName = "Default";

int PingCounter::g_currentPing = -1;
ULONGLONG PingCounter::g_lastPingUpdate = 0;
int PingCounter::g_pingUpdateInterval = 1000;
bool PingCounter::g_serverKnown = false;
char PingCounter::g_serverIP[64] = "";
unsigned short PingCounter::g_serverPort = 0;
std::string PingCounter::g_manualHost = "";
int PingCounter::g_manualPort = 19132;

// ──────────────────────────────────────────────
// UDP endpoint capture via ws2_32 hooks.
// The Bedrock client speaks RakNet over UDP, so all
// non-DNS UDP traffic from this process belongs to the
// game connection. We snapshot the server endpoint from
// sendto()/WSASendTo() (destination) and recvfrom()/
// WSARecvFrom() (source) and use it to ping the server.
// ──────────────────────────────────────────────
namespace {

    int (WINAPI* o_sendto)(SOCKET, const char*, int, int, const struct sockaddr*, int) = nullptr;
    int (WINAPI* o_recvfrom)(SOCKET, char*, int, int, struct sockaddr*, int*) = nullptr;
    int (WINAPI* o_WSASendTo)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, const struct sockaddr*, int, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE) = nullptr;
    int (WINAPI* o_WSARecvFrom)(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD, struct sockaddr*, LPINT, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE) = nullptr;
    int (WINAPI* o_send)(SOCKET, const char*, int, int) = nullptr;
    int (WINAPI* o_recv)(SOCKET, char*, int, int) = nullptr;
    int (WINAPI* o_WSASend)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE) = nullptr;
    int (WINAPI* o_WSARecv)(SOCKET, LPWSABUF, DWORD, LPDWORD, LPDWORD, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE) = nullptr;

    sockaddr_storage g_serverAddr = {};
    socklen_t g_serverAddrLen = 0;
    bool g_hooksInstalled = false;
    SOCKET g_pingSock = INVALID_SOCKET;

    void ResetPingState(); // defined below the UDP pinger state

    // DNS-adopted server bookkeeping (see UpdateServerSelection)
    bool g_adoptedFromDns = false;
    ULONGLONG g_adoptedDnsTime = 0;
    ULONGLONG g_lastPongTime = 0;
    bool g_manualOverride = false;
    struct DnsEntry;
    DnsEntry* NewestDns(ULONGLONG afterTime); // defined with the DNS hooks

    bool IsPrivateV4(const sockaddr_in* in) {
        ULONG a = ntohl(in->sin_addr.s_addr); // host byte order
        if ((a & 0xFF000000) == 0x00000000) return true;  // 0.0.0.0/8 unspecified
        if ((a & 0xFF000000) == 0x7F000000) return true;  // 127.0.0.0/8 loopback
        if ((a & 0xFF000000) == 0x0A000000) return true;  // 10.0.0.0/8
        if ((a & 0xFFF00000) == 0xAC100000) return true;  // 172.16.0.0/12
        if ((a & 0xFFFF0000) == 0xC0A80000) return true;  // 192.168.0.0/16
        if ((a & 0xFFFF0000) == 0xA9FE0000) return true;  // 169.254.0.0/16 link-local
        return false;
    }

    bool IsPrivateV6(const sockaddr_in6* in6) {
        const BYTE* b = in6->sin6_addr.s6_addr;
        if (b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0 && b[4] == 0 && b[5] == 0 &&
            b[6] == 0 && b[7] == 0 && b[8] == 0 && b[9] == 0 && b[10] == 0 && b[11] == 0 &&
            b[12] == 0 && b[13] == 0 && b[14] == 0 && b[15] == 1) return true; // ::1
        if ((b[0] & 0xFE) == 0xFC) return true;                 // fc00::/7 unique local
        if (b[0] == 0xFE && (b[1] & 0xC0) == 0x80) return true; // fe80::/10 link-local
        return false;
    }

    bool IsCandidateV4(const sockaddr_in* in) {
        ULONG a = ntohl(in->sin_addr.s_addr);
        USHORT port = ntohs(in->sin_port);
        if (port == 0 || port == 53) return false;              // DNS / empty
        if ((a & 0xF0000000) == 0xE0000000) return false;        // multicast
        if (a == 0xFFFFFFFF) return false;                       // broadcast
        return !IsPrivateV4(in);
    }

    bool IsCandidate(const sockaddr* sa) {
        if (!sa) return false;
        if (sa->sa_family == AF_INET) {
            return IsCandidateV4(reinterpret_cast<const sockaddr_in*>(sa));
        } else if (sa->sa_family == AF_INET6) {
            const sockaddr_in6* in6 = reinterpret_cast<const sockaddr_in6*>(sa);
            USHORT port = ntohs(in6->sin6_port);
            if (port == 0 || port == 53) return false;
            if (IN6_IS_ADDR_MULTICAST(&in6->sin6_addr)) return false;
            return !IsPrivateV6(in6);
        }
        return false;
    }

    bool AddrsEqual(const sockaddr* a, const sockaddr* b) {
        if (!a || !b || a->sa_family != b->sa_family) return false;
        if (a->sa_family == AF_INET) {
            const sockaddr_in* x = reinterpret_cast<const sockaddr_in*>(a);
            const sockaddr_in* y = reinterpret_cast<const sockaddr_in*>(b);
            return x->sin_port == y->sin_port && x->sin_addr.s_addr == y->sin_addr.s_addr;
        } else if (a->sa_family == AF_INET6) {
            const sockaddr_in6* x = reinterpret_cast<const sockaddr_in6*>(a);
            const sockaddr_in6* y = reinterpret_cast<const sockaddr_in6*>(b);
            return x->sin6_port == y->sin6_port &&
                   memcmp(&x->sin6_addr, &y->sin6_addr, sizeof(IN6_ADDR)) == 0;
        }
        return false;
    }

    // ──────────────────────────────────────────────
    // Endpoint tracking. The server browser fires one-shot
    // UNCONNECTED_PING to every listed server, so the sockets
    // see many transient endpoints. Only an endpoint with
    // SUSTAINED traffic (the real connection) is promoted to
    // g_serverAddr; otherwise the counter would bounce between
    // every server-list IP.
    // ──────────────────────────────────────────────
    const int kMaxTracked = 32;
    const int kPromoteCount = 5;      // packets seen before promoting an endpoint
    const int kStaleAfterMs  = 3000;  // current server silent this long => may replace
    const int kForgetAfterMs = 5000;  // endpoint older than this is not considered

    struct EndpointStat {
        sockaddr_storage addr = {};
        socklen_t len = 0;
        int count = 0;
        ULONGLONG lastSeen = 0;
        bool valid = false;
    };
    EndpointStat g_tracked[kMaxTracked];

    void TrackEndpoint(const sockaddr* sa) {
        if (!IsCandidate(sa)) return;
        ULONGLONG now = GetTickCount64();

        for (int i = 0; i < kMaxTracked; i++) {
            if (!g_tracked[i].valid) continue;
            if (g_tracked[i].addr.ss_family == sa->sa_family &&
                AddrsEqual(reinterpret_cast<const sockaddr*>(&g_tracked[i].addr), sa)) {
                g_tracked[i].count++;
                g_tracked[i].lastSeen = now;
                return;
            }
        }

        int slot = -1;
        ULONGLONG oldest = MAXULONGLONG;
        for (int i = 0; i < kMaxTracked; i++) {
            if (!g_tracked[i].valid) { slot = i; break; }
            if (g_tracked[i].lastSeen < oldest) { oldest = g_tracked[i].lastSeen; slot = i; }
        }
        g_tracked[slot].valid = true;
        g_tracked[slot].count = 1;
        g_tracked[slot].lastSeen = now;
        g_tracked[slot].len = (sa->sa_family == AF_INET) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
        memcpy(&g_tracked[slot].addr, sa, g_tracked[slot].len);
    }

    void SetServer(const sockaddr* sa, socklen_t len) {
        PingCounter::g_serverKnown = true;
        g_serverAddrLen = len;
        memcpy(&g_serverAddr, sa, len);

        char host[NI_MAXHOST] = "";
        if (getnameinfo(reinterpret_cast<const sockaddr*>(&g_serverAddr), g_serverAddrLen,
                        host, sizeof(host), nullptr, 0, NI_NUMERICHOST) == 0) {
            snprintf(PingCounter::g_serverIP, sizeof(PingCounter::g_serverIP), "%s", host);
        }
        PingCounter::g_serverPort = (unsigned short)ntohs(
            g_serverAddr.ss_family == AF_INET
                ? reinterpret_cast<const sockaddr_in*>(&g_serverAddr)->sin_port
                : reinterpret_cast<const sockaddr_in6*>(&g_serverAddr)->sin6_port);
    }

    // ──────────────────────────────────────────────
    // DNS resolution capture. If the client routes game traffic
    // through a local proxy, the only public endpoint we can see is
    // whatever hostname the game resolves before joining. Hook the
    // resolvers and remember recent hostname -> public IP mappings so
    // the pinger can target the real server directly.
    // ──────────────────────────────────────────────
    const int kMaxDns = 16;
    struct DnsEntry {
        char host[128] = {};
        char service[16] = {};
        sockaddr_storage addr = {};
        socklen_t len = 0;
        ULONGLONG time = 0;
        bool valid = false;
    };
    DnsEntry g_dns[kMaxDns];
    int g_dnsCount = 0;
    int g_dnsSlot = 0;

    void RecordDns(const char* host, const char* service, const sockaddr* sa, socklen_t len) {
        if (!host || !host[0]) return;
        if (!IsCandidate(sa)) return; // public only

        DnsEntry* e = &g_dns[g_dnsSlot];
        g_dnsSlot = (g_dnsSlot + 1) % kMaxDns;
        if (g_dnsCount < kMaxDns) g_dnsCount++;

        e->valid = true;
        strncpy(e->host, host, sizeof(e->host) - 1);
        e->host[sizeof(e->host) - 1] = 0;
        e->service[0] = 0;
        if (service) {
            strncpy(e->service, service, sizeof(e->service) - 1);
            e->service[sizeof(e->service) - 1] = 0;
        }
        memcpy(&e->addr, sa, len);
        e->len = len;
        e->time = GetTickCount64();
    }

    DnsEntry* NewestDns(ULONGLONG afterTime) {
        DnsEntry* best = nullptr;
        ULONGLONG bestTime = afterTime;
        for (int i = 0; i < g_dnsCount; i++) {
            if (!g_dns[i].valid) continue;
            if (!g_dns[i].service[0]) continue; // no port -> cannot ping
            if (g_dns[i].time > bestTime) { bestTime = g_dns[i].time; best = &g_dns[i]; }
        }
        return best;
    }

    void UpdateServerSelection() {
        ULONGLONG now = GetTickCount64();

        // Manual override (user forced a hostname:port) always wins
        if (g_manualOverride && PingCounter::g_serverKnown) return;

        if (PingCounter::g_serverKnown) {
            if (g_adoptedFromDns) {
                // DNS-adopted: keep while it answers pings; also honor a fresh
                // newer resolution (user joined a different server)
                DnsEntry* newer = NewestDns(g_adoptedDnsTime);
                if ((now - g_lastPongTime) < 8000ULL) {
                    if (newer) {
                        bool differs = newer->addr.ss_family != g_serverAddr.ss_family ||
                                       !AddrsEqual(reinterpret_cast<const sockaddr*>(&newer->addr),
                                                   reinterpret_cast<const sockaddr*>(&g_serverAddr));
                        if (differs && (now - newer->time) < 3000ULL) {
                            SetServer(reinterpret_cast<const sockaddr*>(&newer->addr), newer->len);
                            g_adoptedDnsTime = newer->time;
                            ResetPingState();
                            return;
                        }
                    }
                    return; // keep current DNS-adopted server
                }
                // stale: fall through and re-select
            } else {
                // UDP-tracked: keep while it still has recent traffic
                int curIdx = -1;
                for (int i = 0; i < kMaxTracked; i++) {
                    if (!g_tracked[i].valid) continue;
                    if (g_tracked[i].addr.ss_family == g_serverAddr.ss_family &&
                        AddrsEqual(reinterpret_cast<const sockaddr*>(&g_tracked[i].addr),
                                   reinterpret_cast<const sockaddr*>(&g_serverAddr))) {
                        curIdx = i;
                        break;
                    }
                }
                if (curIdx >= 0 && (now - g_tracked[curIdx].lastSeen) < (ULONGLONG)kStaleAfterMs) return;
            }
        }

        // 1) Sustained public UDP endpoint (direct connection)
        int bestUdp = -1;
        for (int i = 0; i < kMaxTracked; i++) {
            if (!g_tracked[i].valid) continue;
            if (g_tracked[i].count >= kPromoteCount &&
                (now - g_tracked[i].lastSeen) < (ULONGLONG)kForgetAfterMs) {
                if (bestUdp == -1 || g_tracked[i].count > g_tracked[bestUdp].count) bestUdp = i;
            }
        }
        if (bestUdp >= 0) {
            bool changed = !PingCounter::g_serverKnown ||
                           g_tracked[bestUdp].addr.ss_family != g_serverAddr.ss_family ||
                           !AddrsEqual(reinterpret_cast<const sockaddr*>(&g_tracked[bestUdp].addr),
                                       reinterpret_cast<const sockaddr*>(&g_serverAddr));
            SetServer(reinterpret_cast<const sockaddr*>(&g_tracked[bestUdp].addr), g_tracked[bestUdp].len);
            g_adoptedFromDns = false;
            if (changed) ResetPingState();
            return;
        }

        // 2) Fall back to the most recent DNS resolution (proxy / tunnel setups)
        DnsEntry* dns = NewestDns(0);
        if (dns) {
            bool changed = !PingCounter::g_serverKnown ||
                           dns->addr.ss_family != g_serverAddr.ss_family ||
                           !AddrsEqual(reinterpret_cast<const sockaddr*>(&dns->addr),
                                       reinterpret_cast<const sockaddr*>(&g_serverAddr));
            SetServer(reinterpret_cast<const sockaddr*>(&dns->addr), dns->len);
            g_adoptedFromDns = true;
            g_adoptedDnsTime = dns->time;
            if (changed) ResetPingState();
            return;
        }

        // 3) Nothing usable: drop the phantom server
        if (PingCounter::g_serverKnown) {
            PingCounter::g_serverKnown = false;
            PingCounter::g_serverIP[0] = 0;
            PingCounter::g_serverPort = 0;
            ResetPingState();
        }
    }

    int WINAPI hk_sendto(SOCKET s, const char* buf, int len, int flags, const struct sockaddr* to, int tolen) {
        int r = o_sendto(s, buf, len, flags, to, tolen);
        if (s != g_pingSock) TrackEndpoint(to);
        return r;
    }

    int WINAPI hk_recvfrom(SOCKET s, char* buf, int len, int flags, struct sockaddr* from, int* fromlen) {
        int result = o_recvfrom(s, buf, len, flags, from, fromlen);
        if (result > 0 && s != g_pingSock) TrackEndpoint(from);
        return result;
    }

    int WINAPI hk_WSASendTo(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesSent,
                            DWORD dwFlags, const struct sockaddr* lpTo, int iTolen,
                            LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine) {
        int r = o_WSASendTo(s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpTo, iTolen, lpOverlapped, lpCompletionRoutine);
        if (s != g_pingSock) TrackEndpoint(lpTo);
        return r;
    }

    int WINAPI hk_WSARecvFrom(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesRecvd,
                              LPDWORD lpFlags, struct sockaddr* lpFrom, LPINT lpFromlen,
                              LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine) {
        int result = o_WSARecvFrom(s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpFrom, lpFromlen, lpOverlapped, lpCompletionRoutine);
        if (result >= 0 && s != g_pingSock) TrackEndpoint(lpFrom);
        return result;
    }

    // The RakNet client may use connected UDP (connect()+send()/recv() without an
    // address). Those calls carry no address argument, so recover the peer via
    // getpeername() and only consider UDP sockets (RakNet traffic).
    void TrackPeer(SOCKET s) {
        if (s == g_pingSock) return;
        int type = 0;
        int tlen = sizeof(type);
        if (getsockopt(s, SOL_SOCKET, SO_TYPE, (char*)&type, &tlen) != 0) return;
        if (type != SOCK_DGRAM) return;
        sockaddr_storage peer = {};
        int plen = (int)sizeof(peer);
        if (getpeername(s, reinterpret_cast<sockaddr*>(&peer), &plen) == 0) {
            TrackEndpoint(reinterpret_cast<const sockaddr*>(&peer));
        }
    }

    int WINAPI hk_send(SOCKET s, const char* buf, int len, int flags) {
        int r = o_send(s, buf, len, flags);
        TrackPeer(s);
        return r;
    }

    int WINAPI hk_recv(SOCKET s, char* buf, int len, int flags) {
        int r = o_recv(s, buf, len, flags);
        if (r > 0) TrackPeer(s);
        return r;
    }

    int WINAPI hk_WSASend(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesSent,
                          DWORD dwFlags, LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine) {
        int r = o_WSASend(s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpOverlapped, lpCompletionRoutine);
        TrackPeer(s);
        return r;
    }

    int WINAPI hk_WSARecv(SOCKET s, LPWSABUF lpBuffers, DWORD dwBufferCount, LPDWORD lpNumberOfBytesRecvd,
                          LPDWORD lpFlags, LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine) {
        int r = o_WSARecv(s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpOverlapped, lpCompletionRoutine);
        if (r > 0) TrackPeer(s);
        return r;
    }

    int (WINAPI* o_getaddrinfo)(PCSTR, PCSTR, const ADDRINFOA*, PADDRINFOA*) = nullptr;
    int (WINAPI* o_GetAddrInfoExW)(PCWSTR, PCWSTR, DWORD, LPVOID, const ADDRINFOW*, PADDRINFOW*, LPOVERLAPPED, LPLOOKUPSERVICE_COMPLETION_ROUTINE, LPVOID) = nullptr;

    int WINAPI hk_getaddrinfo(PCSTR pNodeName, PCSTR pServiceName, const ADDRINFOA* pHints, PADDRINFOA* ppResult) {
        int r = o_getaddrinfo(pNodeName, pServiceName, pHints, ppResult);
        if (r == 0 && ppResult && *ppResult) {
            for (const ADDRINFOA* ai = *ppResult; ai; ai = ai->ai_next) {
                if (ai->ai_addr) {
                    RecordDns(pNodeName, pServiceName, ai->ai_addr, (socklen_t)ai->ai_addrlen);
                    break;
                }
            }
        }
        return r;
    }

    int WINAPI hk_GetAddrInfoExW(PCWSTR pName, PCWSTR pServiceName, DWORD dwNameSpace, LPVOID lpNspId,
                                 const ADDRINFOW* hints, PADDRINFOW* ppResult, LPOVERLAPPED lpOverlapped,
                                 LPLOOKUPSERVICE_COMPLETION_ROUTINE lpCompletionRoutine, LPVOID lpNameHandle) {
        int r = o_GetAddrInfoExW(pName, pServiceName, dwNameSpace, lpNspId, hints, ppResult, lpOverlapped, lpCompletionRoutine, lpNameHandle);
        if (r == 0 && lpOverlapped == nullptr && ppResult && *ppResult && pName) {
            char host[256] = {};
            WideCharToMultiByte(CP_UTF8, 0, pName, -1, host, sizeof(host) - 1, nullptr, nullptr);
            char service[16] = {};
            if (pServiceName) WideCharToMultiByte(CP_UTF8, 0, pServiceName, -1, service, sizeof(service) - 1, nullptr, nullptr);
            for (const ADDRINFOW* ai = *ppResult; ai; ai = ai->ai_next) {
                if (ai->ai_addr) {
                    sockaddr_storage tmp = {};
                    socklen_t len = (socklen_t)ai->ai_addrlen;
                    if (len > (socklen_t)sizeof(tmp)) len = (socklen_t)sizeof(tmp);
                    memcpy(&tmp, ai->ai_addr, len);
                    RecordDns(host, pServiceName ? service : nullptr, reinterpret_cast<const sockaddr*>(&tmp), len);
                    break;
                }
            }
        }
        return r;
    }

    void ResolveManualHost() {
        if (PingCounter::g_manualHost.empty()) {
            g_manualOverride = false;
            return;
        }
        char portStr[16] = {};
        snprintf(portStr, sizeof(portStr), "%d", PingCounter::g_manualPort);
        addrinfo hints = {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(PingCounter::g_manualHost.c_str(), portStr, &hints, &res) == 0 && res) {
            bool found = false;
            for (addrinfo* ai = res; ai; ai = ai->ai_next) {
                if (ai->ai_addr && IsCandidate(ai->ai_addr)) {
                    SetServer(ai->ai_addr, (socklen_t)ai->ai_addrlen);
                    g_manualOverride = true;
                    g_adoptedFromDns = false;
                    g_adoptedDnsTime = 0;
                    ResetPingState();
                    found = true;
                    break;
                }
            }
            freeaddrinfo(res);
            if (!found) g_manualOverride = false;
        } else {
            g_manualOverride = false;
        }
    }

    void EnsureHooks() {
        if (g_hooksInstalled) return;
        g_hooksInstalled = true;

        HMODULE ws2 = LoadLibraryA("ws2_32.dll");
        if (!ws2) return;

        MH_Initialize(); // no-op if already initialized

        void* pSendTo      = (void*)GetProcAddress(ws2, "sendto");
        void* pRecvFrom    = (void*)GetProcAddress(ws2, "recvfrom");
        void* pWSASendTo   = (void*)GetProcAddress(ws2, "WSASendTo");
        void* pWSARecvFrom = (void*)GetProcAddress(ws2, "WSARecvFrom");
        void* pSend        = (void*)GetProcAddress(ws2, "send");
        void* pRecv        = (void*)GetProcAddress(ws2, "recv");
        void* pWSASend     = (void*)GetProcAddress(ws2, "WSASend");
        void* pWSARecv     = (void*)GetProcAddress(ws2, "WSARecv");
        void* pGetAddrInfo   = (void*)GetProcAddress(ws2, "getaddrinfo");
        void* pGetAddrInfoEx = (void*)GetProcAddress(ws2, "GetAddrInfoExW");

        if (pSendTo)      MH_CreateHook(pSendTo, (LPVOID)hk_sendto, (LPVOID*)&o_sendto);
        if (pRecvFrom)    MH_CreateHook(pRecvFrom, (LPVOID)hk_recvfrom, (LPVOID*)&o_recvfrom);
        if (pWSASendTo)   MH_CreateHook(pWSASendTo, (LPVOID)hk_WSASendTo, (LPVOID*)&o_WSASendTo);
        if (pWSARecvFrom) MH_CreateHook(pWSARecvFrom, (LPVOID)hk_WSARecvFrom, (LPVOID*)&o_WSARecvFrom);
        if (pSend)        MH_CreateHook(pSend, (LPVOID)hk_send, (LPVOID*)&o_send);
        if (pRecv)        MH_CreateHook(pRecv, (LPVOID)hk_recv, (LPVOID*)&o_recv);
        if (pWSASend)     MH_CreateHook(pWSASend, (LPVOID)hk_WSASend, (LPVOID*)&o_WSASend);
        if (pWSARecv)     MH_CreateHook(pWSARecv, (LPVOID)hk_WSARecv, (LPVOID*)&o_WSARecv);
        if (pGetAddrInfo)   MH_CreateHook(pGetAddrInfo, (LPVOID)hk_getaddrinfo, (LPVOID*)&o_getaddrinfo);
        if (pGetAddrInfoEx) MH_CreateHook(pGetAddrInfoEx, (LPVOID)hk_GetAddrInfoExW, (LPVOID*)&o_GetAddrInfoExW);

        MH_EnableHook(MH_ALL_HOOKS);
    }

    // ──────────────────────────────────────────────
    // UDP pinger (RakNet UNCONNECTED_PING / UNCONNECTED_PONG)
    // ──────────────────────────────────────────────
    int g_pingSockFamily = AF_UNSPEC;
    bool g_wsaInited = false;
    int g_pingState = 0;                 // 0 = idle, 1 = awaiting pong

    // High-resolution timing (GetTickCount64 only has ~15ms resolution, which
    // would make sub-50ms pings read incorrectly)
    LARGE_INTEGER g_qpcFreq = {};
    double QpcMs() {
        if (g_qpcFreq.QuadPart == 0) {
            QueryPerformanceFrequency(&g_qpcFreq);
            if (g_qpcFreq.QuadPart == 0) g_qpcFreq.QuadPart = 1;
        }
        LARGE_INTEGER cnt;
        QueryPerformanceCounter(&cnt);
        return (double)cnt.QuadPart * 1000.0 / (double)g_qpcFreq.QuadPart;
    }
    double g_pingSentQpc = 0.0;

    // Sliding window of the last samples for a stable "normal ping" reading
    const int kPingSamples = 4;
    double g_pingSamples[4] = {};
    int g_pingSampleCount = 0;
    int g_pingSampleIdx = 0;

    void ResetPingState() {
        g_pingState = 0;
        g_pingSampleCount = 0;
        g_pingSampleIdx = 0;
        PingCounter::g_currentPing = -1;
        PingCounter::g_lastPingUpdate = 0;
    }

    const BYTE kRakNetMagic[16] = {
        0x00, 0xFF, 0xFF, 0x00, 0xFE, 0xFE, 0xFE, 0xFE,
        0xFD, 0xFD, 0xFD, 0xFD, 0x12, 0x34, 0x56, 0x78
    };

    void EnsurePingSocket() {
        int family = g_serverAddr.ss_family;
        if (family != AF_INET && family != AF_INET6) return;

        if (g_pingSock != INVALID_SOCKET && g_pingSockFamily == family) return;
        if (g_pingSock != INVALID_SOCKET) {
            closesocket(g_pingSock);
            g_pingSock = INVALID_SOCKET;
        }

        if (!g_wsaInited) {
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
            g_wsaInited = true;
        }

        g_pingSock = socket(family, SOCK_DGRAM, IPPROTO_UDP);
        if (g_pingSock == INVALID_SOCKET) return;
        g_pingSockFamily = family;

        u_long nonblocking = 1;
        ioctlsocket(g_pingSock, FIONBIO, &nonblocking);

        // Prevent the socket from being killed by ICMP port-unreachable
        DWORD zero = 0;
        WSAIoctl(g_pingSock, (DWORD)0x9800000C /*SIO_UDP_CONNRESET*/, &zero, sizeof(zero), nullptr, 0, nullptr, nullptr, nullptr);
    }

    void SendPing(ULONGLONG now) {
        BYTE packet[33];
        packet[0] = 0x01; // ID_UNCONNECTED_PING
        uint64_t t = (uint64_t)now;
        memcpy(packet + 1, &t, 8);
        memcpy(packet + 9, kRakNetMagic, 16);
        uint64_t guid = 0x51AEAEAE51EAEA51ull;
        memcpy(packet + 25, &guid, 8);
        g_pingSentQpc = QpcMs();
        sendto(g_pingSock, (const char*)packet, (int)sizeof(packet), 0,
               reinterpret_cast<const sockaddr*>(&g_serverAddr), g_serverAddrLen);
    }

    void DrainPong() {
        char buf[512];
        for (int attempts = 0; attempts < 8; attempts++) {
            sockaddr_storage from;
            int fromLen = (int)sizeof(from);
            int n = recvfrom(g_pingSock, buf, (int)sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (n == SOCKET_ERROR) break; // WSAEWOULDBLOCK or WSAECONNRESET

            if (n < 17 || (BYTE)buf[0] != 0x1C) continue; // ID_UNCONNECTED_PONG

            // Locate the RakNet magic regardless of the time-field width used by the server
            bool hasMagic = false;
            for (int off = 4; off <= 20; off++) {
                if (off + 16 <= n && memcmp(buf + off, kRakNetMagic, 16) == 0) {
                    hasMagic = true;
                    break;
                }
            }
            if (!hasMagic) continue;

            double rtt = QpcMs() - g_pingSentQpc;
            if (rtt < 0.0) rtt = 0.0;

            if (g_pingSampleCount < kPingSamples) {
                g_pingSamples[g_pingSampleCount++] = rtt;
            } else {
                g_pingSamples[g_pingSampleIdx] = rtt;
                g_pingSampleIdx = (g_pingSampleIdx + 1) % kPingSamples;
            }

            double sum = 0.0;
            for (int i = 0; i < g_pingSampleCount; i++) sum += g_pingSamples[i];
            PingCounter::g_currentPing = (int)std::llround(sum / g_pingSampleCount);
            g_lastPongTime = GetTickCount64();
            g_pingState = 0;
            return;
        }
    }
}

void PingCounter::Initialize(HudElement* hud) {
    g_pingHud = hud;
}

void PingCounter::Shutdown() {
    if (g_pingSock != INVALID_SOCKET) {
        closesocket(g_pingSock);
        g_pingSock = INVALID_SOCKET;
    }
    ResetPingState();
}

void PingCounter::UpdatePing(ULONGLONG now) {
    if (!g_showPingCounter) return;

    EnsureHooks();
    UpdateServerSelection();

    if (!g_serverKnown) {
        g_currentPing = -1;
        return;
    }

    EnsurePingSocket();
    if (g_pingSock == INVALID_SOCKET) return;

    if (g_pingState == 0) {
        if (now - g_lastPingUpdate >= (ULONGLONG)g_pingUpdateInterval) {
            g_lastPingUpdate = now;
            SendPing(now);
            g_pingState = 1;
        }
    } else {
        DrainPong();
        if (g_pingState == 1 && QpcMs() - g_pingSentQpc > 1500.0) {
            g_currentPing = -1;
            g_pingState = 0;
        }
    }
}

void PingCounter::UpdateAnimation(ULONGLONG now) {
    if (g_showPingCounter && g_pingEnableTime == 0) {
        g_pingEnableTime = now;
        g_pingDisableTime = 0;
    }
    if (!g_showPingCounter && g_pingDisableTime == 0 && g_pingEnableTime > 0) {
        g_pingDisableTime = now;
        g_pingEnableTime = 0;
    }
    
    if (g_pingEnableTime > 0) {
        float enableElapsed = (float)(now - g_pingEnableTime) / 1000.0f;
        g_pingAnim = fminf(1.0f, enableElapsed / 0.4f);
    }
    else if (g_pingDisableTime > 0) {
        float disableElapsed = (float)(now - g_pingDisableTime) / 1000.0f;
        float disableAnim = fminf(1.0f, disableElapsed / 0.3f);
        g_pingAnim = 1.0f - disableAnim;
        if (disableAnim >= 1.0f) {
            g_pingEnableTime = 0;
            g_pingDisableTime = 0;
        }
    }
}

void PingCounter::RenderDisplay(float sw, float sh) {
    if (g_showPingCounter || g_pingAnim > 0.01f) {
        float easedAnim = Animations::EaseOutExpo(g_pingAnim);
        
        char text[64];
        if (g_serverKnown && g_currentPing >= 0) {
            snprintf(text, sizeof(text), "Ping: %d ms", g_currentPing);
        } else if (g_serverKnown) {
            snprintf(text, sizeof(text), "Ping: -- ms");
        } else {
            snprintf(text, sizeof(text), "Ping: --");
        }
        
        ImFont* font = GUI::GetFontByName(g_fontName);
        ImGui::PushFont(font);
        float fontSize = 18.0f * g_pingTextScale;
        ImVec2 textSize = ImGui::CalcTextSize(text);
        textSize.x *= g_pingTextScale;
        textSize.y *= g_pingTextScale;
        
        float paddingX = 8.0f * g_pingTextScale;
        float paddingY = 4.0f * g_pingTextScale;
        
        g_pingHud->size = ImVec2(textSize.x + paddingX * 2, textSize.y + paddingY * 2);
        
        if (g_pingHud->pos.x == 0 && g_pingHud->pos.y == 0) {
            g_pingHud->pos = ImVec2(sw - g_pingHud->size.x - 10, 10);
        }
        
        extern bool g_showMenu;
        if (GUI::IsHudEditable()) {
            g_pingHud->HandleDrag(true);
            g_pingHud->ClampToScreen();
        }
        
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        if (draw && easedAnim > 0.0f) {
            ImVec2 pos = g_pingHud->pos;
            
            if (g_showBackground) {
                ImU32 bgCol = IM_COL32(0, 0, 0, (int)(g_bgOpacity * easedAnim * 255.0f));
                draw->AddRectFilled(pos, ImVec2(pos.x + g_pingHud->size.x, pos.y + g_pingHud->size.y), bgCol, 4.0f);
            }
            
            float textX = pos.x + paddingX;
            float textY = pos.y + paddingY;
            
            if (g_pingTextShadow) {
                float shadowOffset = 1.0f * g_pingTextScale;
                ImU32 shadowCol = ImGui::GetColorU32(ImVec4(g_pingCounterShadowColor.x, g_pingCounterShadowColor.y, g_pingCounterShadowColor.z, g_pingCounterShadowColor.w * easedAnim));
                draw->AddText(font, fontSize, ImVec2(textX + shadowOffset, textY + shadowOffset), shadowCol, text);
            }
            
            ImU32 textCol = ImGui::GetColorU32(ImVec4(g_pingTextColor.x, g_pingTextColor.y, g_pingTextColor.z, g_pingTextColor.w * easedAnim));
            draw->AddText(font, fontSize, ImVec2(textX, textY), textCol, text);
            
            if (GUI::IsHudEditable()) {
                draw->AddRect(pos, ImVec2(pos.x + g_pingHud->size.x, pos.y + g_pingHud->size.y), IM_COL32(0, 150, 255, 200), 0.0f, 0, 2.0f);
            }
        }
        ImGui::PopFont();
    }
}

void PingCounter::RenderMenu() {
    GUI::RenderCustomSwitch("Ping Counter", &g_showPingCounter);
    ImGui::TextWrapped("Measures real UDP (RakNet) latency to the connected server using UNCONNECTED_PING/PONG packets.");
    if (GUI::BeginModuleSettings("Ping Counter", &g_showPingCounter)) {
        if (ImGui::BeginTabBar("PingCounterTabs")) {
            if (ImGui::BeginTabItem("General")) {
                GUI::RenderFontSelect("Font", g_fontName);
                ImGui::SliderFloat("Scale", &g_pingTextScale, 0.5f, 3.0f, "%.2f");
                GUI::RenderCustomSwitch("Show Background", &g_showBackground);
                if (g_showBackground) {
                    ImGui::SliderFloat("Background Opacity", &g_bgOpacity, 0.0f, 1.0f, "%.2f");
                }
                ImGui::Checkbox("Text Shadow", &g_pingTextShadow);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Ping Settings")) {
                if (g_serverKnown) {
                    ImGui::Text("Server: %s:%u (UDP)", g_serverIP, g_serverPort);
                } else {
                    ImGui::Text("Server: not connected");
                }
                if (g_currentPing >= 0) {
                    ImGui::Text("Last ping: %d ms", g_currentPing);
                } else {
                    ImGui::Text("Last ping: --");
                }
                ImGui::SliderInt("Update Interval", &g_pingUpdateInterval, 100, 5000, "%d ms");
                ImGui::TextDisabled("Default 1000 ms = one ping per second");
                if (ImGui::Button("Ping Now")) {
                    ResetPingState();
                }
                ImGui::Separator();
                ImGui::Text("Manual target (DNS resolve):");
                char hostBuf[256];
                strncpy(hostBuf, PingCounter::g_manualHost.c_str(), sizeof(hostBuf) - 1);
                hostBuf[sizeof(hostBuf) - 1] = 0;
                if (ImGui::InputText("Host", hostBuf, sizeof(hostBuf))) {
                    PingCounter::g_manualHost = hostBuf;
                }
                ImGui::InputInt("Port", &PingCounter::g_manualPort);
                ImGui::TextDisabled("Leave Host empty to auto-detect the server");
                if (ImGui::Button("Apply & Resolve")) {
                    ResolveManualHost();
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Manual")) {
                    PingCounter::g_manualHost.clear();
                    g_manualOverride = false;
                    if (PingCounter::g_serverKnown) {
                        PingCounter::g_serverKnown = false;
                        PingCounter::g_serverIP[0] = 0;
                        PingCounter::g_serverPort = 0;
                        ResetPingState();
                    }
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Colors")) {
                ImGui::ColorEdit4("Text Color", (float*)&g_pingTextColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
                ImGui::ColorEdit4("Shadow Color", (float*)&g_pingCounterShadowColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        GUI::EndModuleSettings();
    }
}
