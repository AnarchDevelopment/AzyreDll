#include "Velocity.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Hook.hpp"
#include "Framework/Log.hpp"
#include "Framework/Memory.hpp"
#include "SDK/VTables.hpp"

#include <imgui.h>
#include <MinHook.h>
#include <atomic>

namespace mc {

namespace {

std::atomic<bool> g_active{false};
std::atomic<int> g_chance{100};
std::atomic<int> g_hPercent{100};
std::atomic<int> g_vPercent{100};

std::atomic<unsigned int> g_totalPackets{0};
std::atomic<unsigned int> g_modified{0};
std::atomic<unsigned int> g_skipped{0};
std::atomic<int> g_myId{-1};
std::atomic<bool> g_detected{false};

float g_lastVx = 0.0f;
float g_lastVy = 0.0f;
float g_lastVz = 0.0f;

uint32_t g_rng = 0x12345678u;

uint32_t randNext()
{
    uint32_t x = g_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng = x;
    return x;
}

bool rollChance(int pct)
{
    if (pct <= 0)
        return false;
    if (pct >= 100)
        return true;
    return (randNext() % 100) < (uint32_t)pct;
}

using HandleFn = bool(__fastcall*)(void*, void*, void*, void*, float, float, float, float);
HandleFn oHandle = nullptr;
void* g_target = nullptr;
bool g_installed = false;

bool __fastcall hkHandle(void* self, void* a2, void* a3, void* a4,
                         float f0, float f1, float f2, float f3)
{
    if (g_active.load(std::memory_order_relaxed) && self)
    {
        uintptr_t pkt = reinterpret_cast<uintptr_t>(self);
        uintptr_t pktVt = mem::read<uintptr_t>(pkt);
        if (pktVt == mem::resolve(vt::SetEntityMotionPacket))
        {
            int entityId = 0;
            float vx = 0.0f, vy = 0.0f, vz = 0.0f;
            if (mem::rawRead(&entityId, pkt + 0x08, sizeof(entityId)) &&
                mem::rawRead(&vx, pkt + 0x18, sizeof(vx)) &&
                mem::rawRead(&vy, pkt + 0x1C, sizeof(vy)) &&
                mem::rawRead(&vz, pkt + 0x20, sizeof(vz)))
            {
                if (!g_detected.load(std::memory_order_relaxed))
                {
                    g_myId.store(entityId);
                    g_detected.store(true);
                    MC_LOG("[Velocity] MY_ENTITY_ID = %d", entityId);
                }

                if (entityId == g_myId.load(std::memory_order_relaxed))
                {
                    g_totalPackets++;

                    if (!rollChance(g_chance.load(std::memory_order_relaxed)))
                    {
                        g_skipped++;
                    }
                    else
                    {
                        float hMul = (float)g_hPercent.load(std::memory_order_relaxed) * 0.01f;
                        float vMul = (float)g_vPercent.load(std::memory_order_relaxed) * 0.01f;
                        float nvx = vx * hMul;
                        float nvy = vy * vMul;
                        float nvz = vz * hMul;

                        mem::write<float>(pkt + 0x18, nvx);
                        mem::write<float>(pkt + 0x1C, nvy);
                        mem::write<float>(pkt + 0x20, nvz);

                        g_modified++;
                        g_lastVx = nvx;
                        g_lastVy = nvy;
                        g_lastVz = nvz;
                    }
                }
            }
        }
    }

    if (!oHandle)
        return true;

    bool r = false;
    __try
    {
        r = oHandle(self, a2, a3, a4, f0, f1, f2, f3);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        r = false;
    }
    return r;
}

bool installHook()
{
    if (g_installed)
        return true;
    if (!hook::initialize())
        return false;

    void* target = hook::vtableFunc(vt::SetEntityMotionPacket,
                                    vt::slot::SetEntityMotionPacket::Handle);
    if (!target)
    {
        MC_LOG_ERROR("[Velocity] handle slot4 not readable");
        return false;
    }

    if (!hook::create(target, reinterpret_cast<void*>(&hkHandle),
                      reinterpret_cast<void**>(&oHandle)))
        return false;

    g_target = target;
    g_installed = true;
    MC_LOG("[SUCCESS] Velocity hooked SetEntityMotionPacket::handle @ 0x%p", target);
    return true;
}

}

Velocity::Velocity()
    : Module("Velocity", "",
             Category::Combat, 0)
{
    markHasSettings();
}

void Velocity::onEnable()
{
    g_chance.store(chance_);
    g_hPercent.store(hPercent_);
    g_vPercent.store(vPercent_);

    g_totalPackets.store(0);
    g_modified.store(0);
    g_skipped.store(0);
    g_myId.store(-1);
    g_detected.store(false);
    g_rng = (uint32_t)GetTickCount() ^ 0x12345678u;

    installHook();
    g_active.store(true);
}

void Velocity::onDisable()
{
    g_active.store(false);
}

void Velocity::onShutdown()
{
    g_active.store(false);
    if (g_target)
    {
        MH_DisableHook(g_target);
        g_target = nullptr;
    }
    g_installed = false;
    oHandle = nullptr;
}

void Velocity::drawSettings()
{
    if (widgets::CSlider("Chance##vel", &chance_, 0, 100, "%d%%"))
        g_chance.store(chance_);
    if (ImGui::Button("Legit##vel")) { chance_ = 40; g_chance.store(40); }
    ImGui::SameLine();
    if (ImGui::Button("Low##vel")) { chance_ = 60; g_chance.store(60); }
    ImGui::SameLine();
    if (ImGui::Button("Mid##vel")) { chance_ = 80; g_chance.store(80); }
    ImGui::SameLine();
    if (ImGui::Button("Aggr##vel")) { chance_ = 95; g_chance.store(95); }
    ImGui::SameLine();
    if (ImGui::Button("Max##vel")) { chance_ = 100; g_chance.store(100); }

    if (widgets::CSlider("Horizontal (X/Z)##vel", &hPercent_, 0, 100, "%d%%"))
        g_hPercent.store(hPercent_);
    if (widgets::CSlider("Vertical (Y)##vel", &vPercent_, 0, 100, "%d%%"))
        g_vPercent.store(vPercent_);

    ImGui::Separator();
    ImGui::TextDisabled("Hook: SetEntityMotionPacket (slot 4)%s",
                        g_installed ? "" : " - not installed");
    ImGui::TextDisabled("Modified: %u   Skipped: %u   Total: %u",
                        g_modified.load(), g_skipped.load(), g_totalPackets.load());
    ImGui::TextDisabled("My entity: %d", g_myId.load());
    ImGui::TextDisabled("Last vel: (%.2f, %.2f, %.2f)", g_lastVx, g_lastVy, g_lastVz);
}

MC_REGISTER_MODULE(Velocity);

}
