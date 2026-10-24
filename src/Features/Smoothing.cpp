#include "Features/Smoothing.hpp"

#include "Framework/Log.hpp"
#include "Framework/Memory.hpp"

#include <cmath>
#include <unordered_map>

namespace mc::smooth {

namespace {

// [REMPos] diagnostico: traza el PRIMER actor remoto, 250 lineas, y se
// apaga. Muestra raw (AABB +0xB4) vs true (+0x0C) vs salida suavizada.
// Flip a false cuando no haga falta.
constexpr bool kTraceRemote = true;

} // namespace

namespace detail {

// ============================================================================
// INTERPOLACION CON HAND-OFF SEAMLESS (mejora del entityPos de GitHub).
//
// Base (GitHub): lerp(prevRaw, lastRaw, t) con t = (now - changeMs)/50 fijo.
// Defectos corregidos:
//   1. Reloj: GetTickCount64 (~15.6ms) → t cuantizado → saltos. Ahora QPC.
//   2. Periodo fijo 50ms: con updates mas rapidos t llega a 1 y se clava
//      (freeze); con mas lentos nunca llega. Ahora intervalo EMA por señal.
//   3. Salto en update temprano: el lerp reanclaba en prevRaw (=lastRaw
//      viejo) soltando el tramo intermedio de golpe. Ahora el segmento
//      nuevo ARRANCA EN LA POSICION YA MOSTRADA (hand-off seamless): la
//      salida es continua SIEMPRE, en updates tempranos o tardíos.
//
// Se usa para los actores remotos (ESP) y para la BASE DE LA CAMARA
// (1ª persona): la camara cruda es tick-based (~20Hz) — interpolarla
// por-frame la lleva a la tasa de refresco ("144Hz"), calzando con el
// mundo que el motor tambien renderiza interpolado.
// ============================================================================

struct PredState
{
    Vec3 prev{};              // arranque del segmento actual (= pos mostrada)
    Vec3 cur{};               // target del segmento (= raw al llegar update)
    Vec3 lastRaw{};           // ultimo raw visto (deteccion de cambio)
    double segStartMs = 0.0;  // cuando empezo el segmento
    double lastUpdateMs = 0.0;// cuando cambio raw por ultima vez
    float interval = 50.0f;   // EMA del intervalo entre cambios (ms)
    bool valid = false;
};

std::unordered_map<uintptr_t, PredState>& preds()
{
    static std::unordered_map<uintptr_t, PredState> s;
    return s;
}

// Un paso del interpolador. Devuelve la posicion mostrada AHORA para `raw`.
Vec3 interpStep(PredState& ps, const Vec3& raw, double now)
{
    float t = (float)((now - ps.segStartMs) / (double)ps.interval);
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    Vec3 disp{
        ps.prev.x + (ps.cur.x - ps.prev.x) * t,
        ps.prev.y + (ps.cur.y - ps.prev.y) * t,
        ps.prev.z + (ps.cur.z - ps.prev.z) * t,
    };

    if (!ps.valid)
    {
        ps.prev = raw;
        ps.cur = raw;
        ps.lastRaw = raw;
        ps.segStartMs = now;
        ps.lastUpdateMs = now;
        ps.valid = true;
        return raw;
    }

    float dx = raw.x - ps.lastRaw.x;
    float dy = raw.y - ps.lastRaw.y;
    float dz = raw.z - ps.lastRaw.z;
    float d2 = dx * dx + dy * dy + dz * dz;

    if (d2 > 1e-8f) // ignora ruido float
    {
        if (d2 > 100.0f)
        {
            // Teleport / reuso de address: re-anclar al instante.
            ps.prev = raw;
            ps.cur = raw;
            ps.lastRaw = raw;
            ps.segStartMs = now;
            ps.lastUpdateMs = now;
            return raw;
        }

        double dt = now - ps.lastUpdateMs;
        if (dt > 1.0 && dt < 500.0)
        {
            ps.interval += ((float)dt - ps.interval) * 0.4f;
            if (ps.interval < 30.0f)
                ps.interval = 30.0f;
            if (ps.interval > 250.0f)
                ps.interval = 250.0f;
        }

        // HAND-OFF SEAMLESS: el segmento nuevo arranca donde estamos ahora
        // (no en el cur viejo) → la salida no salta nunca.
        ps.prev = disp;
        ps.cur = raw;
        ps.segStartMs = now;
        ps.lastRaw = raw;
        ps.lastUpdateMs = now;
    }

    return disp;
}

} // namespace detail

Vec3 renderPos(uintptr_t actor, const Vec3& raw)
{
    if (!std::isfinite(raw.x) || !std::isfinite(raw.y) || !std::isfinite(raw.z))
        return raw;

    detail::PredState& ps = detail::preds()[actor];
    Vec3 out = detail::interpStep(ps, raw, detail::nowMs());

    if (kTraceRemote)
    {
        static uintptr_t s_traceActor = 0;
        static int s_traceLines = 0;
        if (s_traceActor == 0)
            s_traceActor = actor;
        if (actor == s_traceActor && s_traceLines < 250)
        {
            Vec3 tp{};
            mem::rawRead(&tp, actor + 0x0C, sizeof(tp));
            MC_LOG("[REMPos] raw=(%.3f,%.3f,%.3f) true=(%.3f,%.3f,%.3f) "
                   "out=(%.3f,%.3f,%.3f) iv=%.0f",
                   raw.x, raw.y, raw.z, tp.x, tp.y, tp.z,
                   out.x, out.y, out.z, ps.interval);
            ++s_traceLines;
        }
    }

    return out;
}

Vec3 interp(uintptr_t id, const Vec3& raw)
{
    if (!std::isfinite(raw.x) || !std::isfinite(raw.y) || !std::isfinite(raw.z))
        return raw;

    return detail::interpStep(detail::preds()[id], raw, detail::nowMs());
}

void clear(uintptr_t id)
{
    auto& s = detail::state();
    auto it = s.find(id);
    if (it != s.end())
        s.erase(it);
    detail::preds().erase(id);
}

} // namespace mc::smooth
