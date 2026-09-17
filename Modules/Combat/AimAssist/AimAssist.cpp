/*
Under an4rch Development Public Source License 1.0
- AimAssist: scanning runs on a native CreateThread (like Timer.cpp).
  Tick() only reads cached data (no heavy work, no crashes).
*/

#include "AimAssist.hpp"
#include "../../../ImGui/imgui.h"
#include "../../../GUI/GUI.hpp"
#include "../../Terminal/Terminal.hpp"
#include <windows.h>
#include <psapi.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <immintrin.h>

bool AimAssist::g_enabled = false;
float AimAssist::g_smoothness = 0.25f;
float AimAssist::g_maxDistance = 25.0f;
float AimAssist::g_fov = 90.0f;
bool AimAssist::g_targetHead = true;
bool AimAssist::g_aimYawPitch = true;
bool AimAssist::g_crosshairPriority = true;
uintptr_t AimAssist::g_targetAddr = 0;
float AimAssist::g_targetDist = 0.0f;
int AimAssist::g_enemiesCount = 0;
int AimAssist::g_cachedRpCount = 0;
ULONGLONG AimAssist::g_enableTime = 0;
ULONGLONG AimAssist::g_disableTime = 0;
uintptr_t AimAssist::g_baseAddress = 0;
std::atomic<uintptr_t> AimAssist::g_lp(0);
std::vector<uintptr_t> AimAssist::g_rpCache;
std::mutex AimAssist::g_rpMtx;
HANDLE AimAssist::g_cacheThread = nullptr;
volatile bool AimAssist::g_cacheRunning = false;
int AimAssist::g_lastLPMatches = 0;
int AimAssist::g_lastLPRegions = 0;
std::string AimAssist::g_myName = "";

// Direct read from current process with SEH per block (no handle needed).
static bool SafeReadBlock(uintptr_t addr, void* out, size_t n) {
    __try { memcpy(out, (const void*)addr, n); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

template<typename T>
static bool SafeRead(uintptr_t addr, T& out) {
    if (!addr) return false;
    return SafeReadBlock(addr, &out, sizeof(T));
}

template<typename T>
static bool SafeWrite(uintptr_t addr, const T& value) {
    if (!addr) return false;
    __try {
        *reinterpret_cast<T*>(addr) = value;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ============================================================
// Enumerate committed memory regions
// ============================================================
struct MemRegion { uintptr_t base; size_t size; };

static std::vector<MemRegion> EnumerateRegions() {
    std::vector<MemRegion> out;
    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t addr = 0;
    while (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == sizeof(mbi)) {
        // Fast filter: only committed private heap memory with read/write access.
        // Skips PE images, DLLs, mapped texture buffers, and huge GPU regions.
        if (mbi.State == MEM_COMMIT &&
            mbi.Type == MEM_PRIVATE &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) &&
            !(mbi.Protect & PAGE_GUARD) &&
            mbi.RegionSize <= 64 * 1024 * 1024) {
            out.push_back({ reinterpret_cast<uintptr_t>(mbi.BaseAddress), mbi.RegionSize });
        }
        uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= addr) break;
        addr = next;
    }
    return out;
}

static void ScanRegionForPointer(const MemRegion& r, uintptr_t targetPtr, std::vector<uintptr_t>& out) {
    if (!r.base || r.size < sizeof(uintptr_t)) return;
    uintptr_t start = (r.base + 7) & ~7ULL;
    uintptr_t end = (r.base + r.size) - sizeof(uintptr_t);
    if (start > end) return;

    __try {
        const uintptr_t* cur = reinterpret_cast<const uintptr_t*>(start);
        const uintptr_t* last = reinterpret_cast<const uintptr_t*>(end);

        __m256i targetVec = _mm256_set1_epi64x(targetPtr);
        for (; cur + 4 <= last; cur += 4) {
            __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(cur));
            __m256i cmp = _mm256_cmpeq_epi64(chunk, targetVec);
            int mask = _mm256_movemask_epi8(cmp);
            if (mask != 0) {
                if (cur[0] == targetPtr) out.push_back(reinterpret_cast<uintptr_t>(cur + 0));
                if (cur[1] == targetPtr) out.push_back(reinterpret_cast<uintptr_t>(cur + 1));
                if (cur[2] == targetPtr) out.push_back(reinterpret_cast<uintptr_t>(cur + 2));
                if (cur[3] == targetPtr) out.push_back(reinterpret_cast<uintptr_t>(cur + 3));
            }
        }
        for (; cur <= last; ++cur) {
            if (*cur == targetPtr) {
                out.push_back(reinterpret_cast<uintptr_t>(cur));
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void ScanRegionForPointers2(const MemRegion& r, uintptr_t ptr1, uintptr_t ptr2, std::vector<uintptr_t>& out) {
    if (!r.base || r.size < sizeof(uintptr_t)) return;
    uintptr_t start = (r.base + 7) & ~7ULL;
    uintptr_t end = (r.base + r.size) - sizeof(uintptr_t);
    if (start > end) return;

    __try {
        const uintptr_t* cur = reinterpret_cast<const uintptr_t*>(start);
        const uintptr_t* last = reinterpret_cast<const uintptr_t*>(end);

        __m256i v1 = _mm256_set1_epi64x(ptr1);
        __m256i v2 = _mm256_set1_epi64x(ptr2);
        for (; cur + 4 <= last; cur += 4) {
            __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(cur));
            __m256i c1 = _mm256_cmpeq_epi64(chunk, v1);
            __m256i c2 = _mm256_cmpeq_epi64(chunk, v2);
            __m256i combined = _mm256_or_si256(c1, c2);
            int mask = _mm256_movemask_epi8(combined);
            if (mask != 0) {
                uintptr_t val0 = cur[0]; if (val0 == ptr1 || val0 == ptr2) out.push_back(reinterpret_cast<uintptr_t>(cur + 0));
                uintptr_t val1 = cur[1]; if (val1 == ptr1 || val1 == ptr2) out.push_back(reinterpret_cast<uintptr_t>(cur + 1));
                uintptr_t val2 = cur[2]; if (val2 == ptr1 || val2 == ptr2) out.push_back(reinterpret_cast<uintptr_t>(cur + 2));
                uintptr_t val3 = cur[3]; if (val3 == ptr1 || val3 == ptr2) out.push_back(reinterpret_cast<uintptr_t>(cur + 3));
            }
        }
        for (; cur <= last; ++cur) {
            uintptr_t v = *cur;
            if (v == ptr1 || v == ptr2) {
                out.push_back(reinterpret_cast<uintptr_t>(cur));
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static std::vector<uintptr_t> ScanAllForPointer(uintptr_t targetPtr, const std::vector<MemRegion>& regions) {
    std::vector<uintptr_t> results;
    if (!targetPtr) return results;
    for (const auto& r : regions) {
        ScanRegionForPointer(r, targetPtr, results);
    }
    return results;
}

// ============================================================
// Helpers
// ============================================================
bool AimAssist::IsValidPos(float x, float y, float z) {
    if (fabsf(x) < 0.001f && fabsf(y) < 0.001f && fabsf(z) < 0.001f) return false;
    return (x > -30000000.f && x < 30000000.f && y > -64.f && y < 320.f && z > -30000000.f && z < 30000000.f);
}

// Pitch in [-90,90], Yaw is continuous (unbounded) in Minecraft. Verify finite values.
static bool IsValidAng(float yaw, float pitch) {
    return (!std::isnan(yaw) && !std::isinf(yaw) &&
            !std::isnan(pitch) && !std::isinf(pitch) &&
            pitch >= -90.5f && pitch <= 90.5f);
}

float AimAssist::ShortestDelta(float current, float target) {
    float d = fmodf(target - current, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    else if (d < -180.0f) d += 360.0f;
    return d;
}

bool AimAssist::ReadPos(uintptr_t addr, float* x, float* y, float* z) {
    if (!addr) return false;
    float tx, ty, tz;
    if (!SafeRead(addr + OFFSET_POS_X, tx) || !SafeRead(addr + OFFSET_POS_Y, ty) || !SafeRead(addr + OFFSET_POS_Z, tz))
        return false;
    if (!IsValidPos(tx, ty, tz)) return false;
    *x = tx; *y = ty; *z = tz;
    return true;
}

bool AimAssist::ReadAng(uintptr_t addr, float* yaw, float* pitch) {
    if (!addr) return false;
    float y = 0.f, p = 0.f;
    if (!SafeRead(addr + OFFSET_YAW, y) || !SafeRead(addr + OFFSET_PITCH, p)) return false;
    if (!IsValidAng(y, p)) return false;
    *yaw = y;
    *pitch = p;
    return true;
}

bool AimAssist::ReadName(uintptr_t addr, char* out, size_t maxLen) {
    if (!addr || !out || maxLen < 20) return false;
    char buf[24] = { 0 };
    if (!SafeReadBlock(addr + OFFSET_NAME, buf, 20)) return false;
    buf[20] = '\0';

    size_t len = 0;
    while (len < 20 && buf[len] != '\0') {
        char c = buf[len];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
        len++;
    }
    if (len < 3 || len > 16) return false;
    buf[len] = '\0';
    strcpy_s(out, maxLen, buf);
    return true;
}

bool AimAssist::IsAlive(uintptr_t addr) {
    if (!addr) return false;

    // 1. Height check: when dead and lying on the ground, hitbox height collapses (< 0.4f)
    // Standing: 1.8f, sneaking: 1.5f, swimming/elytra: 0.6f.
    float height = 0.0f;
    if (SafeRead(addr + OFFSET_HEIGHT, height)) {
        if (height < 0.4f) return false;
    }

    // 2. Health check: int health at offset 0x220 (alive: 1-20, dead: <= 0)
    int health = 0;
    if (SafeRead(addr + OFFSET_HEALTH, health)) {
        if (health <= 0) return false;
    }

    return true;
}

void AimAssist::WriteAng(uintptr_t addr, float yaw, float pitch) {
    if (!IsValidAng(yaw, pitch)) return;
    SafeWrite(addr + OFFSET_YAW, yaw);
    if (g_aimYawPitch) {
        SafeWrite(addr + OFFSET_PITCH, pitch);
    }
}

// ============================================================
// Find / Refresh (called from cache thread)
// ============================================================
void AimAssist::FindLocalPlayer() {
    if (!g_baseAddress) return;
    uintptr_t vtable = g_baseAddress + VTABLE_LOCALPLAYER;

    auto regions = EnumerateRegions();
    g_lastLPRegions = (int)regions.size();

    std::vector<uintptr_t> matches;
    for (const auto& r : regions) {
        matches.clear();
        ScanRegionForPointer(r, vtable, matches);
        for (uintptr_t a : matches) {
            // Must be 16-byte aligned (all heap C++ player objects)
            if ((a & 0xF) != 0) continue;

            float x = 0.f, y = 0.f, z = 0.f;
            if (!ReadPos(a, &x, &y, &z)) continue;

            float yaw = 0.f, pitch = 0.f;
            if (!ReadAng(a, &yaw, &pitch)) continue;

            char name[32] = { 0 };
            if (ReadName(a, name, sizeof(name))) {
                g_myName = name;
            } else {
                g_myName.clear();
            }

            g_lp.store(a);
            g_lastLPMatches = 1;
            return;
        }
    }

    g_lp.store(0);
}

void AimAssist::RefreshRemotePlayers() {
    if (!g_baseAddress) return;
    uintptr_t vtable1 = g_baseAddress + VTABLE_REMOTEPLAYER;
    uintptr_t vtable2 = g_baseAddress + VTABLE_REMOTEPLAYER_ALT;

    auto regions = EnumerateRegions();

    std::vector<uintptr_t> raw;
    for (const auto& r : regions) {
        ScanRegionForPointers2(r, vtable1, vtable2, raw);
    }
    std::sort(raw.begin(), raw.end());
    raw.erase(std::unique(raw.begin(), raw.end()), raw.end());

    uintptr_t lp = g_lp.load();
    std::vector<uintptr_t> filtered;
    for (uintptr_t rp : raw) {
        if (rp == lp) continue;

        float x, y, z;
        if (!ReadPos(rp, &x, &y, &z)) continue;

        // 1. Must be alive (skip dead bodies waiting to respawn)
        if (!IsAlive(rp)) continue;

        // 2. Must have a valid player gamertag (rejects empty/dummy/hologram NPCs)
        char name[32] = { 0 };
        if (!ReadName(rp, name, sizeof(name))) continue;

        if (!g_myName.empty() && g_myName == name) continue;

        // 3. Filter out named server NPCs (case-insensitive keyword check)
        std::string lower = name;
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

        filtered.push_back(rp);
    }

    std::lock_guard<std::mutex> lk(g_rpMtx);
    g_rpCache = std::move(filtered);
    g_cachedRpCount = (int)g_rpCache.size();
}

// ============================================================
// Cache thread
// ============================================================
void AimAssist::CacheThreadFn() {
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
            {
                std::lock_guard<std::mutex> lk(g_rpMtx);
                g_rpCache.clear();
                g_cachedRpCount = 0;
            }
            g_targetAddr = 0;
            g_targetDist = 0;
            g_enemiesCount = 0;
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

void AimAssist::StartCacheThread() {
    if (g_cacheRunning && g_cacheThread) return;
    g_cacheRunning = true;
    g_cacheThread = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        CacheThreadFn();
        return 0;
    }, nullptr, 0, nullptr);
}

// ============================================================
// Module lifecycle
// ============================================================
void AimAssist::Initialize(uintptr_t gameBase) {
    if (gameBase) {
        g_baseAddress = gameBase;
    } else {
        g_baseAddress = (uintptr_t)GetModuleHandleW(L"Minecraft.Win10.DX11.exe");
        if (!g_baseAddress) g_baseAddress = (uintptr_t)GetModuleHandleW(nullptr);
    }

    StartCacheThread();
}

void AimAssist::Enable() {
    g_enabled = true;
    g_enableTime = GetTickCount64();
    StartCacheThread();
}

void AimAssist::Disable() {
    g_enabled = false;
    g_disableTime = GetTickCount64();
}

// ============================================================
// Tick - render thread, only reads cache (fast, no crash)
// ============================================================
void AimAssist::Tick() {
    if (!g_enabled) return;

    uintptr_t lp = g_lp.load();
    if (!lp) { g_targetAddr = 0; g_targetDist = 0; g_enemiesCount = 0; return; }

    float mx, my, mz, myaw, mpitch;
    if (!ReadPos(lp, &mx, &my, &mz) || !ReadAng(lp, &myaw, &mpitch)) return;

    // Fast: read cached RPs (no scanning on render thread)
    std::vector<uintptr_t> rps;
    {
        std::lock_guard<std::mutex> lk(g_rpMtx);
        rps = g_rpCache;
    }

    struct Candidate { uintptr_t addr; float dx, dy, dz, dist, targetYaw, deltaYaw; };
    std::vector<Candidate> enemies;

    for (uintptr_t rp : rps) {
        if (rp == lp) continue;
        float ex, ey, ez;
        if (!ReadPos(rp, &ex, &ey, &ez)) continue;

        // Real-time check: if target died during combat, stop aiming immediately
        if (!IsAlive(rp)) continue;

        float dx = ex - mx, dy = ey - my, dz = ez - mz;
        float dist = sqrtf(dx*dx + dy*dy + dz*dz);
        if (dist > g_maxDistance || dist < 0.5f) continue;
        float tYaw = atan2f(-dx, dz) * 180.0f / 3.14159265f;
        float dYaw = fabsf(ShortestDelta(myaw, tYaw));
        if (dYaw > g_fov * 0.5f) continue;
        enemies.push_back({rp, dx, dy, dz, dist, tYaw, dYaw});
    }

    g_enemiesCount = (int)enemies.size();
    if (enemies.empty()) { g_targetAddr = 0; g_targetDist = 0; return; }

    if (g_crosshairPriority)
        std::sort(enemies.begin(), enemies.end(), [](const Candidate& a, const Candidate& b){ return a.deltaYaw < b.deltaYaw; });
    else
        std::sort(enemies.begin(), enemies.end(), [](const Candidate& a, const Candidate& b){ return a.dist < b.dist; });

    Candidate& target = enemies[0];
    g_targetAddr = target.addr;
    g_targetDist = target.dist;

    float aimYOff = g_targetHead ? 1.5f : 0.9f;
    float dyAim = target.dy + aimYOff;
    float hDist = sqrtf(target.dx*target.dx + target.dz*target.dz);
    float targetYaw   = atan2f(-target.dx, target.dz) * 180.0f / 3.14159265f;
    // MCBE: pitch negative = look up, positive = look down  →  negate atan result
    float targetPitch = -atan2f(dyAim, hDist) * 180.0f / 3.14159265f;

    float smooth = g_smoothness;
    float deltaYaw = ShortestDelta(myaw, targetYaw);
    float newYaw = myaw + deltaYaw * smooth;

    float newPitch = mpitch;
    if (g_aimYawPitch) {
        float deltaPitch = targetPitch - mpitch;
        newPitch = mpitch + deltaPitch * smooth;
        if (newPitch < -90.0f) newPitch = -90.0f;
        if (newPitch > 90.0f) newPitch = 90.0f;
    }

    WriteAng(lp, newYaw, newPitch);
}

void AimAssist::RenderMenu() {
    StartCacheThread();

    bool prev = g_enabled;
    GUI::RenderCustomSwitch("AimAssist", &g_enabled);
    if (prev != g_enabled) {
        if (g_enabled) Enable(); else Disable();
    }

    if (GUI::BeginModuleSettings("AimAssist", &g_enabled)) {
        GUI::RenderSlider("Smoothness", &g_smoothness, 0.02f, 1.0f, "%.2f");
        GUI::RenderSlider("Max Distance", &g_maxDistance, 5.0f, 80.0f, "%.0f");
        GUI::RenderSlider("FOV", &g_fov, 10.0f, 360.0f, "%.0f");
        GUI::RenderCustomSwitch("Target Head", &g_targetHead);
        GUI::RenderCustomSwitch("Yaw + Pitch", &g_aimYawPitch);
        GUI::RenderCustomSwitch("Crosshair Priority", &g_crosshairPriority);

        ImGui::Spacing();
        if (g_targetAddr)
            ImGui::TextDisabled("Target: Locked (%.1fm)", g_targetDist);
        else
            ImGui::TextDisabled("Target: None");

        uintptr_t curLp = g_lp.load();
        float curYaw = 0.0f, curPitch = 0.0f;
        if (curLp) ReadAng(curLp, &curYaw, &curPitch);
        ImGui::TextDisabled("Player: %s (0x%llX)", g_myName.c_str(), (unsigned long long)curLp);
        ImGui::TextDisabled("Base: 0x%llX  Angles: Yaw=%.1f  Pitch=%.1f", (unsigned long long)g_baseAddress, curYaw, curPitch);
        ImGui::TextDisabled("Matches: %d  Regions: %d  Cached: %d  Enemies: %d", g_lastLPMatches, g_lastLPRegions, g_cachedRpCount, g_enemiesCount);
        ImGui::TextDisabled("Cache Thread: %s", g_cacheRunning ? "Running" : "Stopped");

        if (GUI::RenderButton("Force Re-Scan LP")) {
            g_lp.store(0);
            FindLocalPlayer();
        }

        GUI::EndModuleSettings();
    }
}