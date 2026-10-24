#pragma once

#include "Framework/Math.hpp"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <deque>
#include <unordered_map>

namespace mc::smooth {

// ============================================================================
// Smoothing compartido (camara, ESP/NameTags/Tracers, Blink, Backtrack).
//
// RELOJ DE ALTA RESOLUCION (QPC): GetTickCount64 tiene granularidad ~15.6ms
// en Windows (timer del sistema). Cualquier suavizado por-frame medido con
// el avanza a saltos (0 / 15.6 / 31.2 ms por frame) — el "trabado" que se
// ve en TODOS los modulos que proyectan a pantalla (comparten la camara).
//
// CHASE-LERP (delayMs <= 0): interpola prev→cur con periodo adaptativo.
// BUFFER DE HISTORIA (delayMs > 0): evalua la senal "delayMs en el pasado"
// interpolando entre samples con timestamp (camara 1ª persona, ghost del
// Backtrack 150ms, caja del Blink 60ms).
// ============================================================================

// Canales dedicados (no colisionan con las addresses de entidades,
// que usan su propia address como id).
namespace channel {
constexpr uintptr_t Camera = 0x0C4EE31;    // camara 1ª persona
constexpr uintptr_t BlinkBox = 0x811E880; // caja del Blink (lastSent)
constexpr uintptr_t Cosmetics = 0xC057A1Cull; // ancla local (alas/halo)
constexpr uintptr_t Ambient = 0xA3B17A21ull;  // centro del AmbientParticles
} // namespace channel

// Mascara del ghost del Backtrack (canal derivado de la address del
// target, sin pisar el canal de entidades del ESP).
constexpr uintptr_t kGhostMask = 0xB7AC37AFC0DEull;

namespace detail {

// Reloj QPC en milisegundos (double, sub-microsegundo por tick).
inline double nowMs()
{
    static const double msPerTick = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart ? 1000.0 / static_cast<double>(f.QuadPart) : 1.0;
    }();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) * msPerTick;
}

struct Sample
{
    double t;
    Vec3 pos;
};

struct Channel
{
    // Buffer de historia (delayMs > 0)
    std::deque<Sample> samples;
    double seen = 0.0;
    // Chase-lerp (delayMs <= 0)
    Vec3 prevRaw{};
    Vec3 lastRaw{};
    double changeMs = 0.0;
    float period = 50.0f; // intervalo observado entre updates del actor
    bool valid = false;
};

inline std::unordered_map<uintptr_t, Channel>& state()
{
    static std::unordered_map<uintptr_t, Channel> s;
    return s;
}

constexpr size_t kMaxSamples = 24;

} // namespace detail

inline Vec3 value(uintptr_t id, const Vec3& raw, float delayMs = 50.0f)
{
    if (!std::isfinite(raw.x) || !std::isfinite(raw.y) || !std::isfinite(raw.z))
        return raw;

    double now = detail::nowMs();
    detail::Channel& ch = detail::state()[id];
    ch.seen = now;

    if (delayMs <= 0.0f)
    {
        // --- Chase-lerp: seguir al target en vivo, sin retraso neto ---
        if (!ch.valid)
        {
            ch.prevRaw = raw;
            ch.lastRaw = raw;
            ch.changeMs = now;
            ch.period = 50.0f;
            ch.valid = true;
            return raw;
        }
        if (raw.x != ch.lastRaw.x || raw.y != ch.lastRaw.y ||
            raw.z != ch.lastRaw.z)
        {
            float interval = (float)(now - ch.changeMs);
            if (interval > 1.0f)
            {
                float obs = interval;
                if (obs < 30.0f)
                    obs = 30.0f;
                if (obs > 200.0f)
                    obs = 200.0f;
                ch.period += (obs - ch.period) * 0.35f;
            }
            ch.prevRaw = ch.lastRaw;
            ch.lastRaw = raw;
            ch.changeMs = now;
        }

        float t = (float)((now - ch.changeMs) / (double)ch.period);
        if (t < 0.0f)
            t = 0.0f;
        if (t > 1.0f)
            t = 1.0f;
        return {
            ch.prevRaw.x + (ch.lastRaw.x - ch.prevRaw.x) * t,
            ch.prevRaw.y + (ch.lastRaw.y - ch.prevRaw.y) * t,
            ch.prevRaw.z + (ch.lastRaw.z - ch.prevRaw.z) * t,
        };
    }

    // --- Buffer de historia: evaluar "delayMs en el pasado" ---
    bool changed = ch.samples.empty() ||
                   ch.samples.back().pos.x != raw.x ||
                   ch.samples.back().pos.y != raw.y ||
                   ch.samples.back().pos.z != raw.z;
    bool timeNew = ch.samples.empty() || (now - ch.samples.back().t) >= 4.0;
    if (changed || timeNew)
        ch.samples.push_back({now, raw});

    while (ch.samples.size() > detail::kMaxSamples)
        ch.samples.pop_front();

    double target = now - (double)delayMs;

    while (ch.samples.size() > 2 && ch.samples[1].t <= target)
        ch.samples.pop_front();

    if (ch.samples.empty())
        return raw;

    if (ch.samples.size() == 1 || ch.samples.back().t <= target)
        return ch.samples.back().pos;

    if (target <= ch.samples.front().t)
        return ch.samples.front().pos;

    for (size_t i = 0; i + 1 < ch.samples.size(); ++i)
    {
        const detail::Sample& a = ch.samples[i];
        const detail::Sample& b = ch.samples[i + 1];
        if (a.t <= target && target < b.t)
        {
            float span = (float)(b.t - a.t);
            float alpha = span > 0.0f
                              ? (float)((target - a.t) / (double)span)
                              : 1.0f;
            return {
                a.pos.x + (b.pos.x - a.pos.x) * alpha,
                a.pos.y + (b.pos.y - a.pos.y) * alpha,
                a.pos.z + (b.pos.z - a.pos.z) * alpha,
            };
        }
    }
    return ch.samples.back().pos;
}

// Entidades (ESP/NameTags/Tracers): RENDER POS — interpolacion prev→cur
// con HAND-OFF SEAMLESS (mejora del entityPos de GitHub): reloj QPC,
// intervalo EMA por actor, y el segmento nuevo arranca en la posicion ya
// mostrada (cero salto con updates tempranos/tardíos). Definida en
// Smoothing.cpp; fallback a raw si no hay datos.
Vec3 renderPos(uintptr_t actor, const Vec3& raw);

inline Vec3 entityPos(uintptr_t addr, const Vec3& raw)
{
    return renderPos(addr, raw);
}

// Interpolador generico con hand-off seamless (mismo esquema que
// renderPos) para cualquier señal suavizada por-frame. Lo usa la CAMARA
// 1ª persona: su base es tick-based (~20Hz) y despues de interpolar
// por-frame queda a la tasa de refresco del monitor ("144Hz"), calzando
// con el mundo que el motor renderiza interpolado.
Vec3 interp(uintptr_t id, const Vec3& raw);

// Limpia el estado de value() y de renderPos()/interp() para `id`.
void clear(uintptr_t id);

inline void gc()
{
    double now = detail::nowMs();
    auto& s = detail::state();
    for (auto it = s.begin(); it != s.end();)
    {
        if (now - it->second.seen > 3000.0)
            it = s.erase(it);
        else
            ++it;
    }
}

} // namespace mc::smooth
