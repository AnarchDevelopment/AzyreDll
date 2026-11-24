#include "Features/LagHistory.hpp"

#include "Features/Smoothing.hpp"

#include <cmath>
#include <deque>

namespace mc::lgh {

namespace {

struct Sample
{
    double t = 0.0;
    Vec3 p{};
};

// 64 muestras alcanzan para 1s de delay a ~20Hz de paquetes (con margen).
constexpr size_t kMaxSamples = 64;
constexpr size_t kMaxActors = 48;
constexpr double kSampleGapMs = 3.0;
// El servidor entrega los paquetes en rafagas: varias muestras separadas por
// pocos ms producen un tramo de velocidad enorme (dt~0 con delta grande) que
// se ve como un tic. Por debajo de esto se COLAPSAN en una sola muestra (la
// ultima posicion manda), igual que hace un reseampleador.
constexpr double kCoalesceMs = 12.0;
// Nada mas viejo que esto puede entrar en ninguna ventana.
constexpr double kRetentionMs = 5000.0;

struct Store : std::unordered_map<uintptr_t, std::deque<Sample>>
{
};

Store& store()
{
    static Store s;
    return s;
}

bool samePoint(const Vec3& a, const Vec3& b)
{
    const Vec3 d = a - b;
    return d.x * d.x + d.y * d.y + d.z * d.z < 1e-8f;
}

void pruneActor(std::deque<Sample>& dq, double now)
{
    while (dq.size() > kMaxSamples)
        dq.pop_front();
    while (dq.size() > 2 && (now - dq.front().t) > kRetentionMs)
        dq.pop_front();
}

// Hermite cubico con tiempos irregulares: C1 (la velocidad no salta entre
// muestras, que es lo que produce el stuttering de un lerp lineal). La
// tangente de cada extremo es la secante de sus vecinos (Catmull-Rom) y se
// limita para que no sobreoscila cuando la muestra viene de una rafaga.
Vec3 hermite(const Vec3& p0, const Vec3& p1, const Vec3& ma, const Vec3& mb,
             double dt, float t)
{
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    const float h10 = t3 - 2.0f * t2 + t;
    const float h01 = -2.0f * t3 + 3.0f * t2;
    const float h11 = t3 - t2;
    const float d = (float)dt;

    return {
        h00 * p0.x + h10 * d * ma.x + h01 * p1.x + h11 * d * mb.x,
        h00 * p0.y + h10 * d * ma.y + h01 * p1.y + h11 * d * mb.y,
        h00 * p0.z + h10 * d * ma.z + h01 * p1.z + h11 * d * mb.z,
    };
}

// Tangente en `i` con clamp de magnitud respecto a la secante del tramo.
Vec3 tangentAt(const std::deque<Sample>& dq, size_t i, const Vec3& a, const Vec3& b,
               double segSpan)
{
    Vec3 m{};
    if (i > 0 && i + 1 < dq.size())
    {
        const Sample& prev = dq[i - 1];
        const Sample& next = dq[i + 1];
        const double span = next.t - prev.t;
        if (span > 1e-6)
            m = Vec3{(next.p.x - prev.p.x) / (float)span,
                     (next.p.y - prev.p.y) / (float)span,
                     (next.p.z - prev.p.z) / (float)span};
    }

    if (m.x == 0.0f && m.y == 0.0f && m.z == 0.0f)
    {
        if (segSpan <= 1e-6)
            return m;
        return {(b.x - a.x) / (float)segSpan, (b.y - a.y) / (float)segSpan,
                (b.z - a.z) / (float)segSpan};
    }

    // Limite: |m| <= 2 * |secante| (evita overshoot en aceleraciones bruscas).
    if (segSpan > 1e-6)
    {
        const float secLen = std::sqrt((b.x - a.x) * (b.x - a.x) +
                                       (b.y - a.y) * (b.y - a.y) +
                                       (b.z - a.z) * (b.z - a.z)) / (float)segSpan;
        const float mLen = std::sqrt(m.x * m.x + m.y * m.y + m.z * m.z);
        const float cap = secLen * 2.0f;
        if (mLen > cap && mLen > 1e-6f)
        {
            const float k = cap / mLen;
            m.x *= k;
            m.y *= k;
            m.z *= k;
        }
    }
    return m;
}

} // namespace

void record(uintptr_t actor, const Vec3& pos)
{
    if (actor == 0)
        return;
    if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z))
        return;

    auto& s = store();
    auto it = s.find(actor);
    if (it == s.end())
    {
        if (s.size() >= kMaxActors)
            s.erase(s.begin());
        it = s.emplace(actor, std::deque<Sample>{}).first;
    }

    std::deque<Sample>& dq = it->second;
    const double now = smooth::detail::nowMs();

    if (!dq.empty())
    {
        const Sample& last = dq.back();
        const double dt = now - last.t;

        // Rafaga del servidor: quedarse con la ultima posicion, no apilar
        // muestras practicamente simultaneas.
        if (dt < kCoalesceMs)
        {
            if (!samePoint(last.p, pos))
            {
                Vec3& p = dq.back().p;
                p = pos;
                dq.back().t = now;
            }
            return;
        }

        if (samePoint(last.p, pos) && dt < kSampleGapMs)
            return;
    }

    dq.push_back({now, pos});
    pruneActor(dq, now);
}

bool at(uintptr_t actor, double delayMs, Vec3& out)
{
    auto& s = store();
    auto it = s.find(actor);
    if (it == s.end() || it->second.empty())
        return false;

    std::deque<Sample>& dq = it->second;
    const double now = smooth::detail::nowMs();
    pruneActor(dq, now);

    if (dq.size() < 2)
    {
        out = dq.front().p;
        return true;
    }

    const double target = now - delayMs;
    if (delayMs <= 0.0 || target <= dq.front().t)
    {
        out = dq.front().p; // aun no hay historial suficiente
        return true;
    }
    if (target >= dq.back().t)
    {
        out = dq.back().p;
        return true;
    }

    for (size_t i = 0; i + 1 < dq.size(); ++i)
    {
        const Sample& a = dq[i];
        const Sample& b = dq[i + 1];
        if (a.t <= target && target < b.t)
        {
            const double span = b.t - a.t;
            if (span <= 1e-6)
            {
                out = b.p;
                return true;
            }

            const float k = (float)((target - a.t) / span);
            const Vec3 ma = tangentAt(dq, i, a.p, b.p, span);
            const Vec3 mb = tangentAt(dq, i + 1, a.p, b.p, span);
            out = hermite(a.p, b.p, ma, mb, span, k);
            return true;
        }
    }

    out = dq.back().p;
    return true;
}

double oldestAgeMs(uintptr_t actor)
{
    auto& s = store();
    auto it = s.find(actor);
    if (it == s.end() || it->second.empty())
        return 0.0;
    return smooth::detail::nowMs() - it->second.front().t;
}

void clear(uintptr_t actor)
{
    store().erase(actor);
}

void clear()
{
    store().clear();
}

} // namespace mc::lgh
