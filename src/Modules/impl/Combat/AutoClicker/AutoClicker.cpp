#include "AutoClicker.hpp"

#include "Features/Friends.hpp"
#include "Features/HitFeed.hpp"
#include "Features/Raycast.hpp"
#include "Features/Smoothing.hpp"
#include "Framework/Log.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Widgets.hpp"
#include "SDK/Core/Game.hpp"
#include "SDK/Engine/Memory/Offsets.hpp"
#include "SDK/Network/Packets/InteractPacket.hpp"

#include <imgui.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

namespace mc {
namespace {

// El click nativo de Bedrock valida ~3 bloques para entidades; por encima de
// eso el server descarta el attack. El rango configurado manda, pero el
// objetivo se elige hasta range_ para que el usuario pueda probar sin
// cambiar de modulo.
constexpr float kAngleEpsilon = 0.01f;

Vec3 viewDir(float yawDeg, float pitchDeg)
{
    const float y = deg2rad(yawDeg);
    const float p = deg2rad(pitchDeg);
    const float cp = std::cos(p);
    return {-std::sin(y) * cp, -std::sin(p), std::cos(y) * cp};
}

// Ojo del jugador: centro horizontal (mPosition +0x0C) y pies del AABB (+0xB4).
Vec3 eyePos(const Actor& actor)
{
    const Vec3 exact = actor.posExact();
    return {exact.x, actor.pos().y + off::gameconst::EyeHeight, exact.z};
}

// Ray vs AABB (slab): distancia de entrada dentro de [0, maxDist].
bool rayAabb(const Vec3& origin, const Vec3& dir, const Vec3& min, const Vec3& max,
             float maxDist, float& outDist)
{
    float tMin = 0.0f;
    float tMax = maxDist;
    const float o[3] = {origin.x, origin.y, origin.z};
    const float d[3] = {dir.x, dir.y, dir.z};
    const float lo[3] = {min.x, min.y, min.z};
    const float hi[3] = {max.x, max.y, max.z};

    for (int i = 0; i < 3; ++i)
    {
        if (std::fabs(d[i]) < 1e-6f)
        {
            if (o[i] < lo[i] || o[i] > hi[i])
                return false;
            continue;
        }
        const float inv = 1.0f / d[i];
        float t1 = (lo[i] - o[i]) * inv;
        float t2 = (hi[i] - o[i]) * inv;
        if (t1 > t2)
            std::swap(t1, t2);
        if (t1 > tMin)
            tMin = t1;
        if (t2 < tMax)
            tMax = t2;
        if (tMin > tMax)
            return false;
    }
    outDist = tMin;
    return true;
}

double randomJitterPercent()
{
    static thread_local std::mt19937 rng{std::random_device{}()};
    static thread_local std::uniform_real_distribution<double> dist(-1.0, 1.0);
    return dist(rng);
}

} // namespace

AutoClicker::AutoClicker()
    : Module("AutoClicker", "Repeats the native click chain (no synthetic input)",
             Category::Combat, 0)
{
    markHasSettings();
    nextLeftDelayMs_ = intervalMs();
    nextRightDelayMs_ = intervalMs();
}

void AutoClicker::onEnable()
{
    lastLeftMs_ = 0.0;
    lastRightMs_ = 0.0;
    nextLeftDelayMs_ = intervalMs();
    nextRightDelayMs_ = intervalMs();
    destroyActive_ = false;
}

void AutoClicker::onDisable()
{
    stopLeftCycle(true);
}

double AutoClicker::intervalMs() const
{
    const double cps = std::clamp(static_cast<double>(cps_), 1.0, 30.0);
    double base = 1000.0 / cps;
    const double jitter = std::clamp(static_cast<double>(jitter_), 0.0, 100.0) / 100.0;
    if (jitter > 0.0)
        base *= 1.0 + jitter * randomJitterPercent();
    return base < 1.0 ? 1.0 : base;
}

Actor AutoClicker::entityUnderCrosshair(const LocalPlayer& local, const Vec3& eye,
                                        const Vec3& dir) const
{
    Actor best;
    if (!attackEntities_)
        return best;

    const Vec3 ray = normalize(dir);
    if (ray.x == 0.0f && ray.y == 0.0f && ray.z == 0.0f)
        return best;

    Game& game = Game::get();
    const BlockSource blocks = game.blockSource();

    float bestHit = range_;
    Actor coneBest;
    float bestAngle = aimTolerance_;
    float coneBestDistance = range_;

    for (const Actor& candidate : game.remotePlayersSnapshot())
    {
        if (!candidate.valid() || candidate.address() == local.address())
            continue;

        // Caja del MODELO: centro mPosition (+0x0C) +- halfW, pies +0xB4.y.
        // Es el mismo encuadre que usan ESP/NameTags; usar +0xB4 para el
        // centro desplazaba la caja y el rayo podia pasar por al lado.
        float width = 0.0f;
        if (!mem::rawRead(&width, candidate.address() + off::player::AabbWidth,
                          sizeof(width)) ||
            !std::isfinite(width) || width < 0.1f || width > 2.5f)
            width = off::gameconst::PlayerWidth;
        const float half = width * 0.5f;
        const Vec3 center = candidate.posExact();
        const float feetY = candidate.pos().y;
        const float height = candidate.height();
        const Vec3 min{center.x - half, feetY, center.z - half};
        const Vec3 max{center.x + half, feetY + height, center.z + half};

        // Reject distant actors before doing name, friend or raycast work.
        const float closestX = clampf(eye.x, min.x, max.x);
        const float closestY = clampf(eye.y, min.y, max.y);
        const float closestZ = clampf(eye.z, min.z, max.z);
        const float dx = closestX - eye.x;
        const float dy = closestY - eye.y;
        const float dz = closestZ - eye.z;
        if (dx * dx + dy * dy + dz * dz > range_ * range_)
            continue;

        float hitDist = 0.0f;
        if (rayAabb(eye, ray, min, max, range_, hitDist))
        {
            if (hitDist < bestHit)
            {
                const std::string name = candidate.name();
                if (name.empty() || friends::isFriend(name))
                    continue;
                best = candidate;
                bestHit = hitDist;
            }
            continue;
        }

        // Sin hitbox: el AABB del tick de red puede venir desfasado del modelo.
        // Fallback por cono (Aim Tolerance) exigiendo linea de vision libre.
        if (aimTolerance_ <= kAngleEpsilon)
            continue;
        const Vec3 toCenter{center.x - eye.x,
                            feetY + height * 0.5f - eye.y,
                            center.z - eye.z};
        const float dist = vecLength(toCenter);
        if (dist <= 0.05f || dist > range_)
            continue;
        const float angle = rad2deg(std::acos(clampf(dot(ray, toCenter) / dist, -1.0f, 1.0f)));
        if (angle > bestAngle ||
            (std::fabs(angle - bestAngle) <= kAngleEpsilon && dist >= coneBestDistance))
            continue;
        const std::string name = candidate.name();
        if (name.empty() || friends::isFriend(name))
            continue;
        coneBest = candidate;
        bestAngle = angle;
        coneBestDistance = dist;
    }

    if (best.valid())
        return best;
    if (coneBest.valid())
    {
        const Vec3 center = coneBest.posExact();
        const float feetY = coneBest.pos().y;
        const Vec3 toCenter{center.x - eye.x,
                            feetY + coneBest.height() * 0.5f - eye.y,
                            center.z - eye.z};
        const float distance = vecLength(toCenter);
        const RaycastHit wall = raycastBlock(blocks, eye, toCenter, distance);
        if (!wall.hit || wall.distance + 0.1f >= distance)
            return coneBest;
    }
    return Actor{};
}

void AutoClicker::stopLeftCycle(bool sendStop)
{
    const bool wasActive = destroyActive_;
    destroyActive_ = false;
    destroyPos_ = {};
    destroyFace_ = 0;
    if (!wasActive || !sendStop)
        return;

    Game& game = Game::get();
    if (!game.localFound())
        return;
    GameMode gm = game.gameMode();
    // Solo aborta si el GameMode sigue con progreso en curso: un abort sobre
    // un ciclo ya cerrado (bloque roto) mandaria un paquete sin bloque.
    if (gm.valid() && gm.destroyProgress() > 0.0f)
        (void)gm.stopDestroyBlock(game.localAddr());
}

void AutoClicker::doLeftClick(const LocalPlayer& local)
{
    Game& game = Game::get();
    GameMode gm = game.gameMode();
    const Vec3 eye = eyePos(local);
    const Vec3 dir = viewDir(local.yaw(), local.pitch());

    const Actor target = entityUnderCrosshair(local, eye, dir);
    if (target.valid())
    {
        // Con una entidad delante el click real no mina: cierra el ciclo de
        // bloque y manda el mismo InteractPacket de ataque. Evitar
        // GameMode::attack aquí omite la predicción local y sus lecturas
        // repetidas de atributos, que son muy costosas a CPS altos.
        stopLeftCycle(true);
        if (swing_)
            (void)local.swing();

        const bool hit = packets::InteractPacket::sendAttack(target);
        if (hit)
        {
            const ItemInstance held = local.heldItem();
            const float damage = held.valid() ? held.attackDamage() : 1.0f;
            hitfeed::recordHit(target.address(), damage);
        }
        if (debug_)
        {
            static double lastLog = 0.0;
            const double now = smooth::detail::nowMs();
            if (now - lastLog >= 500.0)
            {
                lastLog = now;
                MC_LOG("[AutoClick] packet-attack=%s target=%s dist=%.2f rot=(%.1f,%.1f) aps=%d",
                       hit ? "ok" : "fail", target.name().c_str(),
                       distance3D(eye, target.pos()), local.yaw(), local.pitch(), cps_);
            }
        }
        return;
    }

    if (!gm.valid())
    {
        stopLeftCycle(false);
        return;
    }

    if (breakBlocks_)
    {
        const RaycastHit hit = raycastBlock(game.blockSource(), eye, dir, range_);
        if (hit.hit)
        {
            const int face = hit.face < 0 ? 0 : hit.face;
            if (swing_)
                (void)local.swing();

            if (!destroyActive_ || !(destroyPos_ == hit.pos))
            {
                // Ciclo nuevo: startDestroyBlock arma el PlayerActionPacket de
                // inicio; continueDestroyBlock (onTick) acumula el progreso.
                stopLeftCycle(true);
                destroyPos_ = hit.pos;
                destroyFace_ = face;
                destroyActive_ = gm.startDestroyBlock(local.address(), hit.pos,
                                                      static_cast<uint8_t>(face));
                if (debug_)
                {
                    MC_LOG("[AutoClick] break block=(%d,%d,%d) face=%d start=%s",
                           hit.pos.x, hit.pos.y, hit.pos.z, face,
                           destroyActive_ ? "ok" : "fail");
                }
            }
            return;
        }
    }

    // Aire: swing aislado. Actor::swing arma y envia el AnimatePacket por el
    // sender del actor y anima el brazo local, igual que el click real.
    stopLeftCycle(true);
    if (debug_)
    {
        static double lastLog = 0.0;
        const double now = smooth::detail::nowMs();
        if (now - lastLog >= 500.0)
        {
            lastLog = now;
            MC_LOG("[AutoClick] swing-only eye=(%.2f,%.2f,%.2f) rot=(%.1f,%.1f) remotes=%zu gm=%d",
                   eye.x, eye.y, eye.z, local.yaw(), local.pitch(),
                   Game::get().remotePlayersSnapshot().size(),
                   gm.valid() ? 1 : 0);
        }
    }
    if (swing_)
        (void)local.swing();
}

void AutoClicker::doRightClick(const LocalPlayer& local)
{
    Game& game = Game::get();
    GameMode gm = game.gameMode();
    if (!gm.valid())
        return;

    const ItemInstance held = local.heldItem();
    if (!held.valid())
        return;

    const Vec3 eye = eyePos(local);
    const Vec3 dir = viewDir(local.yaw(), local.pitch());
    const RaycastHit hit = raycastBlock(game.blockSource(), eye, dir, range_);

    // Sobre bloque: useItemOn hace el right-click completo (interact, place
    // local, consume y UseItemPacket). En el aire: useItem usa el item en
    // mano (comer, lanzar, etc.).
    if (hit.hit)
    {
        const int face = hit.face < 0 ? 0 : hit.face;
        (void)gm.useItemOnVt(held, hit.pos, static_cast<uint8_t>(face), hit.point,
                             local.address());
    }
    else
    {
        (void)gm.useItem(held, local.address());
    }

    if (debug_)
    {
        static double lastLog = 0.0;
        const double now = smooth::detail::nowMs();
        if (now - lastLog >= 1000.0)
        {
            lastLog = now;
            MC_LOG("[AutoClick] right-click block=%d aps=%d", hit.hit ? 1 : 0, cps_);
        }
    }
}

void AutoClicker::onFrame(float /*dt*/)
{
    if (!Game::get().localFound() || menu::visible() ||
        Game::get().client().chatOpen())
    {
        stopLeftCycle(true);
        return;
    }

    LocalPlayer local = Game::get().localPlayer();
    if (!local.valid())
    {
        stopLeftCycle(true);
        return;
    }

    if (!left_ && !right_)
        return;

    const bool lmb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool rmb = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    const double now = smooth::detail::nowMs();

    if (left_ && (!holdClick_ || lmb))
    {
        if (lastLeftMs_ == 0.0 || now - lastLeftMs_ >= nextLeftDelayMs_)
        {
            lastLeftMs_ = now;
            nextLeftDelayMs_ = intervalMs();
            doLeftClick(local);
        }
    }
    else
        stopLeftCycle(true);

    if (right_ && (!holdClick_ || rmb))
    {
        if (lastRightMs_ == 0.0 || now - lastRightMs_ >= nextRightDelayMs_)
        {
            lastRightMs_ = now;
            nextRightDelayMs_ = intervalMs();
            doRightClick(local);
        }
    }
}

void AutoClicker::onTick()
{
    // El ciclo de minado lo continua el propio GameMode a ritmo de tick (20Hz),
    // igual que el juego: continueDestroyBlock acumula dt real y solo rompe
    // cuando el progreso llega a 1.0.
    if (!destroyActive_)
        return;

    if (!Game::get().localFound() || menu::visible() ||
        Game::get().client().chatOpen())
    {
        stopLeftCycle(true);
        return;
    }

    LocalPlayer local = Game::get().localPlayer();
    const GameMode gm = Game::get().gameMode();
    if (!local.valid() || !gm.valid())
    {
        stopLeftCycle(false);
        return;
    }

    BlockPos current;
    if (!gm.destroyTarget(current) || !(current == destroyPos_))
    {
        // El juego cerro el ciclo por su cuenta (bloque roto).
        stopLeftCycle(false);
        return;
    }

    (void)gm.continueDestroyBlock(local.address(), current,
                                  static_cast<uint8_t>(destroyFace_));
}

std::string AutoClicker::suffix() const
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%dcps", cps_);
    return buf;
}

nlohmann::json AutoClicker::saveSettings() const
{
    return nlohmann::json{
        {"cps", cps_},
        {"jitter", jitter_},
        {"holdClick", holdClick_},
        {"left", left_},
        {"right", right_},
        {"swing", swing_},
        {"attackEntities", attackEntities_},
        {"breakBlocks", breakBlocks_},
        {"range", range_},
        {"aimTolerance", aimTolerance_},
        {"debug", debug_},
    };
}

void AutoClicker::loadSettings(const nlohmann::json& j)
{
    cps_ = std::clamp(j.value("cps", cps_), 1, 30);
    jitter_ = std::clamp(j.value("jitter", jitter_), 0, 100);
    holdClick_ = j.value("holdClick", holdClick_);
    left_ = j.value("left", left_);
    right_ = j.value("right", right_);
    swing_ = j.value("swing", swing_);
    attackEntities_ = j.value("attackEntities", attackEntities_);
    breakBlocks_ = j.value("breakBlocks", breakBlocks_);
    range_ = clampf(j.value("range", range_), 1.0f, 6.0f);
    aimTolerance_ = clampf(j.value("aimTolerance", aimTolerance_), 0.0f, 45.0f);
    debug_ = j.value("debug", debug_);
    nextLeftDelayMs_ = intervalMs();
    nextRightDelayMs_ = intervalMs();
}

void AutoClicker::drawSettings()
{
    widgets::CSlider("CPS##ac", &cps_, 1, 30, "%d");
    widgets::CSlider("Jitter##ac", &jitter_, 0, 100, "%d%%");

    widgets::Checkbox("Hold Click##ac", &holdClick_);
    widgets::Checkbox("Left Click##ac", &left_);
    widgets::Checkbox("Right Click##ac", &right_);
    widgets::CSlider("Range##ac", &range_, 1.0f, 6.0f, "%.1f");
    widgets::Checkbox("Swing##ac", &swing_);
    widgets::Checkbox("Attack Entities##ac", &attackEntities_);
    if (attackEntities_)
        widgets::CSlider("Aim Tolerance##ac", &aimTolerance_, 0.0f, 45.0f, "%.0f");
    widgets::Checkbox("Break Blocks##ac", &breakBlocks_);
    widgets::Checkbox("Debug##ac", &debug_);
}

MC_REGISTER_MODULE(AutoClicker);

}
