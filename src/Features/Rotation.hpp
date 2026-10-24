#pragma once

#include <windows.h>

#include <atomic>

// ============================================================================
// Sistema GLOBAL de rotaciones server-side (silent rotations).
//
// Como los clientes conocidos (Rise/Solstice adaptado a este build
// pre-1.16): los modulos piden una rotacion con rot::request(...) y:
//   * El hook del MovePlayerPacket saliente la aplica al paquete ANTES
//     de serializar — el SERVER te ve mirando el objetivo (los demas
//     jugadores ven tu cabeza girada).
//   * El actor local se rota en el Present para el render del propio
//     modelo: en 2ª/3ª persona ves tu cabeza girada.
//   * La camara/perspectiva del cliente NUNCA se toca: en 1ª persona tu
//     vista queda intacta.
// La request expira sola (ms) — la mas reciente con mayor prioridad gana.
// ============================================================================

namespace mc::rot {

namespace detail {
inline std::atomic<bool> g_active{false};
inline std::atomic<float> g_pitch{0.0f};
inline std::atomic<float> g_yaw{0.0f};
inline std::atomic<float> g_headYawOffset{0.0f};
inline std::atomic<ULONGLONG> g_expiry{0};
inline std::atomic<int> g_priority{0};
} // namespace detail

// Pedir la rotacion activa. priority mayor = gana sobre requests activas
// de menor prioridad (Combat > Movement > Visuals, por ejemplo).
inline void request(float pitch, float yaw, unsigned ms, int priority,
                    float headYawOffset = 0.0f)
{
    using namespace detail;
    int cur = g_priority.load(std::memory_order_acquire);
    if (g_active.load(std::memory_order_acquire) && priority < cur)
        return; // hay una request de mayor prioridad vigente

    g_pitch.store(pitch, std::memory_order_relaxed);
    g_yaw.store(yaw, std::memory_order_relaxed);
    g_headYawOffset.store(headYawOffset, std::memory_order_relaxed);
    g_expiry.store(GetTickCount64() + (ULONGLONG)ms, std::memory_order_relaxed);
    g_priority.store(priority, std::memory_order_release);
    g_active.store(true, std::memory_order_release);
}

// Rotacion vigente (false si expiro). Refresca la expiracion
// automaticamente — el caller decide si renueva con request().
inline bool current(float& outPitch, float& outYaw, float* outHeadYaw = nullptr)
{
    using namespace detail;
    if (!g_active.load(std::memory_order_acquire))
        return false;
    if (GetTickCount64() > g_expiry.load(std::memory_order_relaxed))
    {
        g_active.store(false, std::memory_order_release);
        g_priority.store(0, std::memory_order_relaxed);
        return false;
    }
    outPitch = g_pitch.load(std::memory_order_relaxed);
    outYaw = g_yaw.load(std::memory_order_relaxed);
    if (outHeadYaw)
        *outHeadYaw = outYaw + g_headYawOffset.load(std::memory_order_relaxed);
    return true;
}

inline void clear()
{
    using namespace detail;
    g_active.store(false, std::memory_order_release);
    g_priority.store(0, std::memory_order_relaxed);
}

// Clear only the current request when it belongs to this priority. This lets
// a module stop its own movement spoof without cancelling a higher-priority
// combat request.
inline void clear(int priority)
{
    using namespace detail;
    if (!g_active.load(std::memory_order_acquire) ||
        g_priority.load(std::memory_order_acquire) != priority)
        return;
    g_active.store(false, std::memory_order_release);
    g_priority.store(0, std::memory_order_relaxed);
}

// Prioridades sugeridas (convencion del proyecto).
constexpr int PrioVisual = 10;   // decorativos
constexpr int PrioMovement = 50; // scaffold, movement helpers
constexpr int PrioCombat = 90;   // aim/aura — siempre gana

} // namespace mc::rot
