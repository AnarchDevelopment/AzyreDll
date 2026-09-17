/*
Under an4rch Development Public Source License 1.0
- ESP (2D world-to-screen). Cache on background thread ~1s,
  render reads live each frame. Boxes use real hitbox height
  with angular projection identical to ESP.py.
*/

#include "ESP.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../GUI/GUI.hpp"
#include "../../Terminal/Terminal.hpp"
#include <windows.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <unordered_map>
#include <immintrin.h>

bool ESP::g_enabled = false;
bool ESP::g_showBox = true;
bool ESP::g_showName = true;
bool ESP::g_showDistance = true;
bool ESP::g_showHealth = true;
bool ESP::g_showTracer = false;
float ESP::g_maxDistance = 256.0f;
float ESP::g_fov = 110.0f;
float ESP::g_eyeHeight = 1.62f;
float ESP::g_boxThickness = 1.5f;
uintptr_t ESP::g_fovAddr = 0;
float ESP::g_boxColor[4]     = { 0.18f, 0.92f, 0.55f, 1.0f };
float ESP::g_nameColor[4]    = { 1.0f, 1.0f, 1.0f, 1.0f };
float ESP::g_tracerColor[4]  = { 0.18f, 0.92f, 0.55f, 0.6f };
float ESP::g_distanceColor[4]= { 0.96f, 0.96f, 0.96f, 0.9f };

uintptr_t ESP::g_baseAddress = 0;
std::atomic<uintptr_t> ESP::g_lp(0);
std::atomic<uintptr_t> ESP::g_mc(0);
std::atomic<uintptr_t> ESP::g_lr(0);
std::vector<uintptr_t> ESP::g_rpCache;
std::mutex ESP::g_rpMtx;
ULONGLONG ESP::g_lastScan = 0;
HANDLE ESP::g_cacheThread = nullptr;
volatile bool ESP::g_cacheRunning = false;
std::string ESP::g_myName = "";

// ─── Helpers ──────────────────────────────────────────────────────
static bool SafeReadBlock(uintptr_t addr, void* out, size_t n) {
    __try { memcpy(out, (const void*)addr, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<typename T>
static bool SafeRead(uintptr_t addr, T& out) {
    if (!addr) return false;
    return SafeReadBlock(addr, &out, sizeof(T));
}

struct MemRegion { uintptr_t base; size_t size; };

static std::vector<MemRegion> EnumerateRegions() {
    std::vector<MemRegion> out;
    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t addr = 0;
    while (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == sizeof(mbi)) {
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) &&
            !(mbi.Protect & PAGE_GUARD) && mbi.RegionSize <= 64*1024*1024)
            out.push_back({reinterpret_cast<uintptr_t>(mbi.BaseAddress), mbi.RegionSize});
        uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= addr) break;
        addr = next;
    }
    return out;
}

static void ScanRegionForPointers2(const MemRegion& r, uintptr_t p1, uintptr_t p2, std::vector<uintptr_t>& out) {
    if (!r.base || r.size < 8) return;
    uintptr_t start = (r.base + 7) & ~7ULL;
    uintptr_t end = (r.base + r.size) - 8;
    if (start > end) return;
    __try {
        auto* cur = reinterpret_cast<const uintptr_t*>(start);
        auto* last = reinterpret_cast<const uintptr_t*>(end);
        __m256i v1 = _mm256_set1_epi64x(p1), v2 = _mm256_set1_epi64x(p2);
        for (; cur + 4 <= last; cur += 4) {
            __m256i c = _mm256_or_si256(
                _mm256_cmpeq_epi64(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(cur)), v1),
                _mm256_cmpeq_epi64(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(cur)), v2));
            if (_mm256_movemask_epi8(c))
                for (int k = 0; k < 4; k++)
                    if (cur[k]==p1 || cur[k]==p2) out.push_back(reinterpret_cast<uintptr_t>(cur+k));
        }
        for (; cur <= last; ++cur)
            if (*cur==p1 || *cur==p2) out.push_back(reinterpret_cast<uintptr_t>(cur));
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ─── Player readers ───────────────────────────────────────────────
bool ESP::IsValidPos(float x, float y, float z) {
    if (fabsf(x)<0.001f && fabsf(y)<0.001f && fabsf(z)<0.001f) return false;
    return x>-30000000.f && x<30000000.f && y>-64.f && y<320.f && z>-30000000.f && z<30000000.f;
}
bool ESP::ReadPos(uintptr_t a, float* x, float* y, float* z) {
    if (!a) return false;
    float tx,ty,tz;
    if (!SafeRead(a+OFFSET_POS_X,tx)||!SafeRead(a+OFFSET_POS_Y,ty)||!SafeRead(a+OFFSET_POS_Z,tz)) return false;
    if (!IsValidPos(tx,ty,tz)) return false;
    *x=tx; *y=ty; *z=tz; return true;
}
bool ESP::ReadAng(uintptr_t a, float* y, float* p) {
    if (!a) return false;
    float yy=0,pp=0;
    if (!SafeRead(a+OFFSET_YAW,yy)||!SafeRead(a+OFFSET_PITCH,pp)) return false;
    if (std::isnan(yy)||std::isinf(yy)||std::isnan(pp)||std::isinf(pp)) return false;
    if (pp<-90.5f||pp>90.5f) return false;
    *y=yy; *p=pp; return true;
}
bool ESP::ReadName(uintptr_t a, char* out, size_t mx) {
    if (!a || !out || mx < 4) return false;
    uintptr_t nameAddr = a + OFFSET_NAME;

    char raw[64] = { 0 };
    size_t strLen = 0, strCap = 0;
    SafeRead(nameAddr + 16, strLen);
    SafeRead(nameAddr + 24, strCap);

    if (strCap < 16 && strLen < 16) {
        if (!SafeReadBlock(nameAddr, raw, min(strLen > 0 ? strLen : 15, sizeof(raw) - 1))) return false;
    } else {
        uintptr_t heapPtr = 0;
        if (SafeRead(nameAddr, heapPtr) && heapPtr > 0x10000) {
            if (!SafeReadBlock(heapPtr, raw, min(strLen < 60 ? strLen : 60, sizeof(raw) - 1))) {
                if (!SafeReadBlock(nameAddr, raw, 15)) return false;
            }
        } else {
            if (!SafeReadBlock(nameAddr, raw, 15)) return false;
        }
    }
    raw[sizeof(raw) - 1] = '\0';

    // Strip Minecraft formatting codes (§c, §l, etc.) and newlines
    char clean[64] = { 0 };
    size_t dst = 0;
    for (size_t src = 0; src < sizeof(raw) && raw[src] != '\0' && dst < mx - 1; src++) {
        if ((unsigned char)raw[src] == 0xA7 || raw[src] == '\xA7') {
            if (raw[src + 1] != '\0') src++;
            continue;
        }
        if (raw[src] == '\r' || raw[src] == '\n') return false; // Hologram multiline text
        unsigned char c = (unsigned char)raw[src];
        if (c >= 32 && c <= 126) {
            clean[dst++] = (char)c;
        }
    }
    clean[dst] = '\0';
    if (dst < 2) return false;

    // Filter server NPCs / Holograms by keywords
    std::string lower = clean;
    for (char& c : lower) c = (char)tolower(c);
    if (lower.find("npc") != std::string::npos ||
        lower.find("shop") != std::string::npos ||
        lower.find("tienda") != std::string::npos ||
        lower.find("merchant") != std::string::npos ||
        lower.find("delivery") != std::string::npos ||
        lower.find("quest") != std::string::npos ||
        lower.find("mision") != std::string::npos ||
        lower.find("server") != std::string::npos ||
        lower.find("bot") != std::string::npos ||
        lower.find("youtube") != std::string::npos ||
        lower.find("sub") != std::string::npos ||
        lower.find("review") != std::string::npos ||
        lower.find("rango") != std::string::npos ||
        lower.find("hologram") != std::string::npos ||
        lower.find("warp") != std::string::npos ||
        lower.find("click") != std::string::npos ||
        lower.find("toca") != std::string::npos) {
        return false;
    }

    strcpy_s(out, mx, clean);
    return true;
}
bool ESP::ReadHeight(uintptr_t a, float* h) { float v = 0; return SafeRead(a + OFFSET_HEIGHT, v) && (*h = v, true); }
bool ESP::ReadHealth(uintptr_t a, int* hp) { int v = 0; return SafeRead(a + OFFSET_HEALTH, v) && (*hp = v, true); }
bool ESP::IsAlive(uintptr_t a) {
    if (!a) return false;
    float h = 0;
    if (ReadHeight(a, &h) && h < 0.4f) return false;
    return true;
}

static float ReadOptionsFov() {
    char appdata[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH)) {
        std::string base = std::string(appdata) + "\\.minecraft_bedrock\\installations";
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA((base + "\\*").c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != '.') {
                    std::string inst = base + "\\" + fd.cFileName;
                    WIN32_FIND_DATAA fd2;
                    HANDLE hFind2 = FindFirstFileA((inst + "\\*").c_str(), &fd2);
                    if (hFind2 != INVALID_HANDLE_VALUE) {
                        do {
                            if ((fd2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd2.cFileName[0] != '.') {
                                std::string optPath = inst + "\\" + fd2.cFileName + "\\packageData\\minecraftpe\\options.txt";
                                FILE* fp = nullptr;
                                if (fopen_s(&fp, optPath.c_str(), "r") == 0 && fp) {
                                    char line[256];
                                    while (fgets(line, sizeof(line), fp)) {
                                        if (strncmp(line, "gfx_field_of_view:", 18) == 0) {
                                            float f = (float)atof(line + 18);
                                            fclose(fp);
                                            FindClose(hFind2);
                                            FindClose(hFind);
                                            if (f >= 30.0f && f <= 130.0f) return f;
                                        }
                                    }
                                    fclose(fp);
                                }
                            }
                        } while (FindNextFileA(hFind2, &fd2));
                        FindClose(hFind2);
                    }
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
    }
    char localAppData[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableA("LOCALAPPDATA", localAppData, MAX_PATH)) {
        std::string uwpOpt = std::string(localAppData) + "\\Packages\\Microsoft.MinecraftUWP_8wekyb3d8bbwe\\LocalState\\games\\com.mojang\\minecraftpe\\options.txt";
        FILE* fp = nullptr;
        if (fopen_s(&fp, uwpOpt.c_str(), "r") == 0 && fp) {
            char line[256];
            while (fgets(line, sizeof(line), fp)) {
                if (strncmp(line, "gfx_field_of_view:", 18) == 0) {
                    float f = (float)atof(line + 18);
                    fclose(fp);
                    if (f >= 30.0f && f <= 130.0f) return f;
                }
            }
            fclose(fp);
        }
    }
    return 110.0f;
}

float ESP::ReadFov() {
    static float diskFov = 0.0f;
    static ULONGLONG lastCheck = 0;
    ULONGLONG now = GetTickCount64();
    if (diskFov == 0.0f || (now - lastCheck > 3000)) {
        diskFov = ReadOptionsFov();
        lastCheck = now;
        if (diskFov >= 30.0f && diskFov <= 130.0f) g_fov = diskFov;
    }
    return g_fov;
}

// ─── Scans (background thread) ───────────────────────────────────
void ESP::FindLocalPlayer() {
    if (!g_baseAddress) return;
    auto regions = EnumerateRegions();
    std::vector<uintptr_t> m;
    uintptr_t vt = g_baseAddress + VTABLE_LOCALPLAYER;
    for (auto& r : regions) ScanRegionForPointers2(r, vt, vt, m);
    for (uintptr_t a : m) {
        if (a & 0xF) continue;
        float x, y, z, yaw, pitch;
        if (!ReadPos(a, &x, &y, &z) || !ReadAng(a, &yaw, &pitch)) continue;
        g_lp.store(a);
        char nm[32] = { 0 };
        g_myName = ReadName(a, nm, sizeof(nm)) ? nm : "";
        uintptr_t mc = 0, lr = 0, mcVt = 0;
        if (SafeRead(a + OFFSET_LP_TO_MC, mc) && mc && SafeRead(mc, mcVt) && mcVt == (g_baseAddress + VTABLE_MINECRAFTCLIENT)) {
            if (SafeRead(mc + OFFSET_MC_TO_LR, lr) && lr) g_lr.store(lr); else g_lr.store(0);
            g_mc.store(mc);
        } else { g_mc.store(0); g_lr.store(0); }
        return;
    }
    g_lp.store(0);
}

void ESP::RefreshRemotePlayers() {
    if (!g_baseAddress) return;
    auto regions = EnumerateRegions();
    std::vector<uintptr_t> raw;
    uintptr_t vt1 = g_baseAddress + VTABLE_REMOTEPLAYER, vt2 = g_baseAddress + VTABLE_REMOTEPLAYER_ALT;
    for (auto& r : regions) ScanRegionForPointers2(r, vt1, vt2, raw);
    std::sort(raw.begin(), raw.end());
    raw.erase(std::unique(raw.begin(), raw.end()), raw.end());
    uintptr_t lp = g_lp.load();
    std::vector<uintptr_t> f;
    for (auto rp : raw) {
        if (rp == lp || (rp & 0xF)) continue;
        float x, y, z;
        if (!ReadPos(rp, &x, &y, &z)) continue;
        if (!IsAlive(rp)) continue;

        // Strictly require valid Xbox Gamertag to eliminate all NPCs, holograms, and dummies
        char nm[32] = { 0 };
        if (!ReadName(rp, nm, sizeof(nm))) continue;
        if (!g_myName.empty() && g_myName == nm) continue;

        std::string lower = nm;
        for (char& c : lower) c = (char)tolower(c);
        if (lower.find("npc") != std::string::npos ||
            lower.find("shop") != std::string::npos ||
            lower.find("merchant") != std::string::npos ||
            lower.find("delivery") != std::string::npos ||
            lower.find("quest") != std::string::npos ||
            lower.find("server") != std::string::npos ||
            lower.find("bot") != std::string::npos) {
            continue;
        }

        f.push_back(rp);
    }
    std::lock_guard<std::mutex> lk(g_rpMtx);
    g_rpCache = std::move(f);
}

void ESP::CacheThreadFn() {
    ULONGLONG lastRpRefresh = 0;
    while (g_cacheRunning) {
        uintptr_t lp = g_lp.load();
        bool lpValid = false;
        if (lp && g_baseAddress) {
            uintptr_t curVtable = 0;
            if (SafeRead(lp, curVtable) && curVtable == (g_baseAddress + VTABLE_LOCALPLAYER)) {
                float x = 0.f, y = 0.f, z = 0.f;
                if (ReadPos(lp, &x, &y, &z)) {
                    lpValid = true;
                }
            }
        }

        if (!lpValid) {
            g_lp.store(0);
            FindLocalPlayer();
            lastRpRefresh = 0;
        }

        ULONGLONG now = GetTickCount64();
        if (g_lp.load() && (now - lastRpRefresh >= 1000)) {
            RefreshRemotePlayers();
            lastRpRefresh = now;
        }

        Sleep(200);
    }
}
static DWORD WINAPI EspCacheThread(LPVOID) { ESP::CacheThreadFn(); return 0; }

void ESP::StartCacheThread() {
    if (g_cacheRunning && g_cacheThread) return;
    g_cacheRunning = true;
    g_cacheThread = CreateThread(0, 0, EspCacheThread, 0, 0, 0);
}

// ─── Lifecycle ────────────────────────────────────────────────────
void ESP::Initialize(uintptr_t gameBase) {
    g_baseAddress = (uintptr_t)GetModuleHandleW(L"Minecraft.Win10.DX11.exe");
    if (!g_baseAddress) g_baseAddress = (uintptr_t)GetModuleHandleW(nullptr);
    if (!g_baseAddress) g_baseAddress = gameBase;
    StartCacheThread();
}
void ESP::Enable() { g_enabled = true; StartCacheThread(); }
void ESP::Disable() { g_enabled = false; }
void ESP::Tick() {}

// ─── Camera data ──────────────────────────────────────────────────
// LR + OFFSET_CAM_VEC stores float4 (fwdX, fwdY, fwdZ, pullBackDist):
//   1st person  → w ≈ -0.04  (no displacement needed)
//   3rd rear    → w ≈ +3.2   (camera behind, dot(vec, playerFwd) > 0)
//   3rd front   → w ≈ +1.3   (camera in front, dot(vec, playerFwd) < 0, vec is flipped)
void ESP::GetCameraData(uintptr_t lp, float& cx, float& cy, float& cz, float& camYaw, float& camPitch) {
    float px = 0, py = 0, pz = 0, yaw = 0, pitch = 0;
    ESP::ReadPos(lp, &px, &py, &pz);
    ESP::ReadAng(lp, &yaw, &pitch);

    // Default (1st person): camera at player eye, angles = player angles
    cx = px;
    cy = py + g_eyeHeight;
    cz = pz;
    camYaw   = yaw;
    camPitch = pitch;

    uintptr_t lr = g_lr.load();
    if (!lr) return;

    float vec[4] = { 0, 0, 0, 0 };
    if (!SafeReadBlock(lr + OFFSET_CAM_VEC, vec, sizeof(vec))) return;

    float pullBack = vec[3];
    // < 0.5  → 1st person (w ≈ -0.047), no adjustment
    // > 10.0 → stale/garbage LR data, ignore
    if (pullBack < 0.5f || pullBack > 10.0f) return;


    // Build player forward vector (same convention as WorldToScreen)
    float yR = yaw   * (3.14159265358979f / 180.0f);
    float pR = pitch * (3.14159265358979f / 180.0f);
    float cosY = cosf(yR), sinY = sinf(yR);
    float cosP = cosf(pR), sinP = sinf(pR);
    float pfx = -sinY * cosP;   // player forward X
    float pfy = -sinP;           // player forward Y
    float pfz =  cosY * cosP;   // player forward Z

    // Normalize camera vec to get direction
    float fwdLen = sqrtf(vec[0]*vec[0] + vec[1]*vec[1] + vec[2]*vec[2]);
    if (fwdLen < 0.5f) return;
    float vx = vec[0] / fwdLen, vy = vec[1] / fwdLen, vz = vec[2] / fwdLen;

    // Dot product tells us rear (>0) vs front (<0)
    float dot = vx * pfx + vy * pfy + vz * pfz;

    if (dot >= 0.0f) {
        // 3rd person REAR: camera behind player, looking in player's forward direction
        cx = px - pfx * pullBack;
        cy = (py + g_eyeHeight) - pfy * pullBack;
        cz = pz - pfz * pullBack;
        camYaw   = yaw;
        camPitch = pitch;
    } else {
        // 3rd person FRONT: camera in front of player, looking at player's back
        // Camera origin is in the player's forward direction; look angle is reversed
        cx = px + pfx * pullBack;
        cy = (py + g_eyeHeight) + pfy * pullBack;
        cz = pz + pfz * pullBack;
        camYaw   = yaw + 180.0f;
        camPitch = -pitch;
    }
}


// ─── Perspective Projection ──────────────────────────────────────
static bool WorldToScreen(float px, float py, float pz,
                          float cx, float cy, float cz,
                          float camYaw, float camPitch, float fovDeg,
                          float sw, float sh, float& sx, float& sy) {
    float yR = camYaw * (3.14159265358979323846f / 180.0f);
    float pR = camPitch * (3.14159265358979323846f / 180.0f);
    float cosY = cosf(yR), sinY = sinf(yR);
    float cosP = cosf(pR), sinP = sinf(pR);

    float fx = -sinY * cosP;
    float fy = -sinP;
    float fz =  cosY * cosP;

    float rx = -cosY;
    float ry = 0.0f;
    float rz = -sinY;

    float ux = -sinY * sinP;
    float uy =  cosP;
    float uz =  cosY * sinP;

    float dx = px - cx;
    float dy = py - cy;
    float dz = pz - cz;

    float viewZ = dx * fx + dy * fy + dz * fz;
    if (viewZ <= 0.1f) return false;

    float viewX = dx * rx + dy * ry + dz * rz;
    float viewY = dx * ux + dy * uy + dz * uz;

    float fovVR = fovDeg * (3.14159265358979323846f / 180.0f);
    float tanHalfV = tanf(fovVR * 0.5f);
    if (tanHalfV <= 0.001f) return false;
    float tanHalfH = tanHalfV * (sw / sh);

    float ndcX = (viewX / viewZ) / tanHalfH;
    float ndcY = (viewY / viewZ) / tanHalfV;

    sx = (ndcX * 0.5f + 0.5f) * sw;
    sy = (0.5f - ndcY * 0.5f) * sh;

    return true;
}

// ─── Render ───────────────────────────────────────────────────────
// Per-entity position smoother (frame-rate-independent exponential)
struct SmoothedPos { float x, y, z; bool valid; };
static std::unordered_map<uintptr_t, SmoothedPos> s_posSmooth;

void ESP::RenderDisplay(float sw, float sh) {
    if (!g_enabled || sw <= 0 || sh <= 0) return;
    uintptr_t lp = g_lp.load();
    if (!lp) return;

    float camX, camY, camZ, camYaw, camPitch;
    GetCameraData(lp, camX, camY, camZ, camYaw, camPitch);

    float fov = ReadFov();
    if (fov < 20.0f || fov > 130.0f) fov = g_fov;

    std::vector<uintptr_t> rps;
    {
        std::lock_guard<std::mutex> lk(g_rpMtx);
        rps = g_rpCache;
    }

    // Frame delta for smoothing (capped at 100ms to avoid big jumps after lag)
    static ULONGLONG s_lastFrameMs = 0;
    ULONGLONG nowMs = GetTickCount64();
    float dtSec = s_lastFrameMs ? fminf((nowMs - s_lastFrameMs) * 0.001f, 0.1f) : 0.016f;
    s_lastFrameMs = nowMs;
    // k=30 → time constant ~33ms; fast enough to feel responsive, smooth enough to hide 50ms tick steps
    float alpha = 1.0f - expf(-30.0f * dtSec);

    // Remove smooth-state entries for players that left the cache
    for (auto it = s_posSmooth.begin(); it != s_posSmooth.end(); ) {
        bool found = false;
        for (auto r : rps) if (r == it->first) { found = true; break; }
        it = found ? std::next(it) : s_posSmooth.erase(it);
    }

    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImU32 bC = ImGui::GetColorU32(ImVec4(g_boxColor[0], g_boxColor[1], g_boxColor[2], g_boxColor[3]));
    ImU32 nC = ImGui::GetColorU32(ImVec4(g_nameColor[0], g_nameColor[1], g_nameColor[2], g_nameColor[3]));
    ImU32 tC = ImGui::GetColorU32(ImVec4(g_tracerColor[0], g_tracerColor[1], g_tracerColor[2], g_tracerColor[3]));
    ImU32 dC = ImGui::GetColorU32(ImVec4(g_distanceColor[0], g_distanceColor[1], g_distanceColor[2], g_distanceColor[3]));
    ImFont* f = GUI::g_fontDefault ? GUI::g_fontDefault : ImGui::GetFont();
    float cx = sw * 0.5f;

    for (auto rp : rps) {
        float ex, ey, ez;
        if (!ReadPos(rp, &ex, &ey, &ez)) continue;
        if (!IsAlive(rp)) continue;

        // Skip any entity that doesn't have a valid player gamertag
        char nm[32] = { 0 };
        if (!ReadName(rp, nm, sizeof(nm))) continue;

        // ── Position smoothing ──────────────────────────────────────
        // Minecraft remote-player positions update at ~20TPS (every 50ms).
        // Exponential lerp eliminates the discrete 50ms jump; the box glides
        // smoothly to the latest server-tick position each render frame.
        {
            auto& sm = s_posSmooth[rp];
            if (!sm.valid) { sm.x = ex; sm.y = ey; sm.z = ez; sm.valid = true; }
            sm.x += (ex - sm.x) * alpha;
            sm.y += (ey - sm.y) * alpha;
            sm.z += (ez - sm.z) * alpha;
            ex = sm.x; ey = sm.y; ez = sm.z;
        }

        float ht = 1.8f;
        ReadHeight(rp, &ht);

        double dist = sqrt((double)(ex - camX) * (ex - camX) + (double)(ey - camY) * (ey - camY) + (double)(ez - camZ) * (ez - camZ));
        if (dist > g_maxDistance || dist < 0.5) continue;

        float fsx, fsy, hsx, hsy;
        if (!WorldToScreen(ex, ey, ez, camX, camY, camZ, camYaw, camPitch, fov, sw, sh, fsx, fsy)) continue;
        if (!WorldToScreen(ex, ey + ht, ez, camX, camY, camZ, camYaw, camPitch, fov, sw, sh, hsx, hsy)) continue;

        float bH = fsy - hsy;
        if (bH < 2.0f || bH > sh * 2.0f) continue;
        float bW = bH * 0.45f;
        float cX = (fsx + hsx) * 0.5f;
        float x1 = cX - bW * 0.5f, y1 = hsy, x2 = cX + bW * 0.5f, y2 = fsy;

        if (y2 < -50 || y1 > sh + 50) continue;
        if (x2 < -200 || x1 > sw + 200) continue;

        if (g_showTracer) d->AddLine(ImVec2(cx, sh), ImVec2(cX, y2), tC, g_boxThickness);
        if (g_showBox) {
            d->AddRect(ImVec2(x1 - 1, y1 - 1), ImVec2(x2 + 1, y2 + 1), IM_COL32(0, 0, 0, 220), 0, 0, g_boxThickness + 1);
            d->AddRect(ImVec2(x1 + 1, y1 + 1), ImVec2(x2 - 1, y2 - 1), IM_COL32(0, 0, 0, 220), 0, 0, g_boxThickness + 1);
            d->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), bC, 0, 0, g_boxThickness);
        }
        if (g_showHealth) {
            int hp = 20;
            ReadHealth(rp, &hp);
            float h = (float)hp / 20.0f;
            h = h < 0 ? 0 : (h > 1 ? 1 : h);
            float bx = x1 - 7;
            d->AddRectFilled(ImVec2(bx - 1, y1 - 1), ImVec2(bx + 4, y2 + 1), IM_COL32(0, 0, 0, 200));
            d->AddRectFilled(ImVec2(bx, y2 - (y2 - y1) * h), ImVec2(bx + 3, y2), IM_COL32((int)(255 * (1 - h)), (int)(255 * h), 35, 255));
        }
        if (g_showName) {
            ImVec2 ts = f->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0, nm);
            d->AddText(ImVec2(cX - ts.x * 0.5f, y1 - ts.y - 2), nC, nm);
        }
        if (g_showDistance) {
            char db[32];
            sprintf_s(db, "%.1fm", (float)dist);
            ImVec2 ts = f->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0, db);
            d->AddText(ImVec2(cX - ts.x * 0.5f, y2 + 2), dC, db);
        }
    }
}

// ─── Menu ─────────────────────────────────────────────────────────
void ESP::RenderMenu(){
    StartCacheThread();
    bool prev=g_enabled;GUI::RenderCustomSwitch("ESP",&g_enabled);
    if(prev!=g_enabled){if(g_enabled)Enable();else Disable();}
    if(GUI::BeginModuleSettings("ESP",&g_enabled)){
        GUI::RenderCustomSwitch("Box##ESP",&g_showBox);
        GUI::RenderCustomSwitch("Name##ESP",&g_showName);
        GUI::RenderCustomSwitch("Distance##ESP",&g_showDistance);
        GUI::RenderCustomSwitch("Health##ESP",&g_showHealth);
        GUI::RenderCustomSwitch("Tracer##ESP",&g_showTracer);
        ImGui::Separator();
        GUI::RenderSlider("Max Distance",&g_maxDistance,8.0f,256.0f,"%.0f");
        GUI::RenderSlider("FOV##ESP",&g_fov,30.0f,130.0f,"%.0f");
        GUI::RenderSlider("Eye Height",&g_eyeHeight,0.0f,2.5f,"%.2f");
        GUI::RenderSlider("Thickness",&g_boxThickness,0.5f,4.0f,"%.1f");
        ImGui::Separator();
        ImGui::ColorEdit4("Box Color##ESP",g_boxColor,ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit4("Name Color##ESP",g_nameColor,ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit4("Distance Color##ESP",g_distanceColor,ImGuiColorEditFlags_NoInputs);
        ImGui::ColorEdit4("Tracer Color##ESP",g_tracerColor,ImGuiColorEditFlags_NoInputs);
        GUI::EndModuleSettings();
    }
}
