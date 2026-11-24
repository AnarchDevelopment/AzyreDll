#include "Backtrack.hpp"
#include "GUI/Widgets.hpp"

#include "Features/Friends.hpp"
#include "Features/LagHistory.hpp"
#include "Features/Smoothing.hpp"
#include "Framework/Log.hpp"
#include "Framework/Math.hpp"
#include "Framework/Memory.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Theme.hpp"
#include "SDK/Client/Camera.hpp"
#include "SDK/Core/Game.hpp"
#include "SDK/Engine/VTable/VTables.hpp"

#include <imgui.h>
#include <windows.h>

#include <cmath>
#include <string>

namespace mc {

namespace {

// Cursor del sistema visible = pantalla del juego abierta (inventario,
// contenedor, chat, pause). Mismo chequeo que InventoryWalk.
bool cursorShowing()
{
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (GetCursorInfo(&ci))
        return (ci.flags & CURSOR_SHOWING) != 0;
    return true;
}

bool lmbDown()
{
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

bool finiteVec(const Vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

float dist2(const Vec3& a, const Vec3& b)
{
    const Vec3 d = a - b;
    return d.x * d.x + d.y * d.y + d.z * d.z;
}

bool writeVec3(uintptr_t actor, uintptr_t off, const Vec3& v)
{
    float f[3] = {v.x, v.y, v.z};
    return mem::rawWrite(actor + off, f, sizeof(f));
}

// Canal de smoothing propio del modulo: el punto historico ya es C1 (Hermite)
// pero llega a la tasa de los paquetes; este interpolador por frame lo lleva a
// la tasa de refresco sin saltos (hand-off seamless, QPC).
constexpr uintptr_t kShownMask = 0x5A0B1EEF33ull;

// Reubica al actor en `where` dejando TODA su geometria consistente: lee el
// AABB actual, calcula sus offsets respecto a la posicion real del actor y
// traslada el box intacto. mPosition (+0x0C), el target de red del moveTo
// (+0xD08) y el AABB (+0xB4..+0xC8) quedan alineados — el hitbox sigue siendo
// el box del propio juego, solo que en `where`.
bool placeActorAt(uintptr_t actor, const Vec3& where)
{
    if (!actor || !mem::isReadable(actor, sizeof(uintptr_t)))
        return false;
    if (!finiteVec(where))
        return false;

    // Anti recycling: si la memoria del actor fue liberada y reutilizada por
    // otro objeto, su vtable ya no es la de un player remoto.
    const uintptr_t vt = mem::read<uintptr_t>(actor);
    if (vt != mem::resolve(vt::RemotePlayer))
        return false;

    Vec3 real{};
    if (!mem::rawRead(&real, actor + off::player::PosTrueX, sizeof(float) * 3))
        return false;
    if (!finiteVec(real))
        return false;

    float box[6] = {};
    if (!mem::rawRead(box, actor + off::player::AabbMinX, sizeof(box)))
        return false;
    for (float f : box)
    {
        if (!std::isfinite(f))
            return false;
    }

    // Box degenerado o absurdo (actor re-spawneando / estado raro): no pintar
    // nada, el siguiente paquete real lo deja como estaba.
    const float w = box[3] - box[0];
    const float h = box[4] - box[1];
    const float d = box[5] - box[2];
    if (w <= 0.0f || h <= 0.0f || d <= 0.0f || w > 10.0f || h > 10.0f || d > 10.0f)
        return false;

    const float offMin[3] = {box[0] - real.x, box[1] - real.y, box[2] - real.z};
    const float offMax[3] = {box[3] - real.x, box[4] - real.y, box[5] - real.z};

    // AABB primero y en un solo write (24 bytes continuos): el hitbox llega a
    // `where` de una pieza.
    const float moved[6] = {
        where.x + offMin[0], where.y + offMin[1], where.z + offMin[2],
        where.x + offMax[0], where.y + offMax[1], where.z + offMax[2],
    };
    if (!mem::rawWrite(actor + off::player::AabbMinX, moved, sizeof(moved)))
        return false;

    writeVec3(actor, off::player::PosTrueX, where);
    writeVec3(actor, off::player::MoveTargetX, where);
    return true;
}

// Posicion del ultimo paquete del server (+0xD08). El actor interpolaba hacia
// aqui: si difiere de lo ultimo que escribimos, llego un paquete nuevo.
bool readNetPos(uintptr_t actor, Vec3& out)
{
    if (!mem::rawRead(&out, actor + off::player::MoveTargetX, sizeof(float) * 3))
        return false;
    return finiteVec(out);
}

} // namespace

Backtrack::Backtrack()
    : Module("Backtrack", "Lag compensation: the target follows its path with delay",
             Category::Combat, 0)
{
    markHasSettings();
}

void Backtrack::onEnable()
{
    targetAddr_ = 0;
    wroteOnce_ = false;
    active_ = false;
    attacking_ = false;
    releaseReason_ = "enabled";
    lgh::clear();
    smooth::clear(smooth::kGhostMask);
}

void Backtrack::stopLag(const char* why)
{
    if (targetAddr_ != 0 && wroteOnce_)
    {
        // Soltar = devolver al actor a su posicion REAL. Equivale a liberar
        // los paquetes retenidos en OpenSlinky: el server ya lo tiene ahi y
        // el siguiente paquete restaura todo solo.
        placeActorAt(targetAddr_, realPos_);
    }
    if (targetAddr_ != 0)
        lgh::clear(targetAddr_);

    // Ids de smoothing del actor: al soltar ni la caja ni el punto mostrado
    // deben arrastrar nada del lag anterior.
    smooth::clear(targetAddr_ ^ smooth::kGhostMask);
    smooth::clear(targetAddr_ ^ kShownMask);

    targetAddr_ = 0;
    wroteOnce_ = false;
    active_ = false;
    releaseReason_ = why;
}

void Backtrack::onDisable()
{
    stopLag("disabled");
}

void Backtrack::onShutdown()
{
    stopLag("shutdown");
}

void Backtrack::onHurt(void* actor, void* source, float& damage, bool& cancel)
{
    (void)actor;
    (void)source;
    (void)damage;
    (void)cancel;

    // "disable_on_hit": el knockback propio mueve al enemigo respecto a ti;
    // seguir retrasandolo solo empeora la lectura. Soltar.
    if (disableOnHit_ && active_)
        stopLag("hurt");
}

void Backtrack::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
    {
        if (active_)
            stopLag("no local");
        return;
    }

    LocalPlayer local = game.localPlayer();
    if (!local.valid())
    {
        if (active_)
            stopLag("local invalid");
        return;
    }

    Vec3 eye = local.posExact();
    eye.y = local.pos().y + off::gameconst::EyeHeight;

    // Require mouse pressed: LMB fisico + sin pantallas del juego.
    attacking_ = lmbDown() && !menu::visible() && !menu::hudEditing() && !cursorShowing();
    const bool want = requireMouse_ ? attacking_ : true;

    // Target: el remoto mas cercano en la ventana.
    uintptr_t best = 0;
    float bestDist = 0.0f;
    for (const Actor& other : game.remotePlayersSnapshot())
    {
        if (!other.valid() || other.address() == local.address())
            continue;

        const std::string name = other.name();
        if (name.empty() || friends::isFriend(name))
            continue;

        const Vec3 p = other.posExact();
        if (!finiteVec(p))
            continue;

        const float d = distance3D(eye, p);
        if (!std::isfinite(d) || d < minDist_ || d > maxDist_)
            continue;
        if (best != 0 && d >= bestDist)
            continue;

        best = other.address();
        bestDist = d;
    }

    if (best != 0)
        lastDist_ = bestDist;

    if (active_)
    {
        // Cambio de target, fuera de rango o solto el click: soltar y
        // devolver al actor a su posicion real.
        if (!want)
        {
            stopLag("released");
            return;
        }
        if (best == 0 || best != targetAddr_)
        {
            stopLag(best == 0 ? "out of range" : "target changed");
            return;
        }
        return;
    }

    if (!want || best == 0)
        return;

    // Arrancar el lag sobre el target elegido.
    targetAddr_ = best;
    wroteOnce_ = false;
    active_ = true;
    releaseReason_ = "lagging";

    // Sembrar el historial con la posicion real actual: el retraso crece de
    // forma progresiva hasta `delayMs` en vez de saltar en el primer frame.
    Vec3 seed{};
    Actor target{best};
    if (target.valid() && readNetPos(best, seed))
    {
        lgh::record(best, seed);
        realPos_ = seed;
    }
    else if (target.valid())
    {
        seed = target.posExact();
        if (finiteVec(seed))
        {
            lgh::record(best, seed);
            realPos_ = seed;
        }
    }

    // El mostrado arranca en la semilla: sin salto al activar.
    smooth::clear(best ^ kShownMask);
    shownPos_ = seed;
}

void Backtrack::onRender()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    if (!local.valid())
        return;

    if (!active_ || targetAddr_ == 0)
        return;

    // Con una pantalla del juego abierta no se toca nada (el tick ya suelta).
    if (menu::visible() || menu::hudEditing() || cursorShowing())
        return;

    Actor target{targetAddr_};
    if (!target.valid())
    {
        stopLag("target gone");
        return;
    }

    // 1) Posicion REAL: el target de red del ultimo paquete, leido ANTES de
    //    nuestro rewrite (despues vale la historica que escribimos).
    Vec3 net{};
    const bool haveNet = readNetPos(targetAddr_, net);
    if (haveNet && (!wroteOnce_ || dist2(net, lastWritten_) > 1e-4f))
    {
        realPos_ = net;
        lgh::record(targetAddr_, net);
    }
    else if (!haveNet)
    {
        const Vec3 cur = target.posExact();
        if (finiteVec(cur) && (!wroteOnce_ || dist2(cur, lastWritten_) > 4e-4f))
        {
            realPos_ = cur;
            lgh::record(targetAddr_, cur);
        }
    }

    // 2) Aplicar el retraso: el actor (modelo + hitbox) va `delayMs` atras.
    //    El historial se evalua con Hermite (C1: la velocidad no salta entre
    //    muestras) y aun se pasa por el interpolador por frame para llevarlo a
    //    la tasa de refresco: asi el enemigo camina fluido, sin stuttering.
    Vec3 hist{};
    if (!lgh::at(targetAddr_, (double)delayMs_, hist) || !finiteVec(hist))
        return;

    shownPos_ = smooth::interp(targetAddr_ ^ kShownMask, hist);
    if (!finiteVec(shownPos_))
        shownPos_ = hist;

    if (placeActorAt(targetAddr_, shownPos_))
    {
        lastWritten_ = shownPos_;
        wroteOnce_ = true;
    }

    if (renderReal_)
        drawRealBox();
}

// Indicador de posicion real: caja en la posicion que tiene el server ahora,
// mientras el enemigo se muestra `delayMs` atras.
void Backtrack::drawRealBox()
{
    if (targetAddr_ == 0)
        return;

    Actor target{targetAddr_};
    if (!target.valid())
        return;

    Game& game = Game::get();
    LocalPlayer local = game.localPlayer();
    if (!local.valid())
        return;

    // Encuadre del ESP: realPos_ vive en espacio mPosition (+0x0C) = CENTRO
    // horizontal, que es donde el motor ancla el modelo. Solo hay que mapear
    // la Y a pies (+0xB4.y) y usar height() (+0xD4).
    float boxHt = 1.8f;
    const float h = target.height();
    if (h > 0.1f)
        boxHt = h;

    const float yOff = target.pos().y - target.posExact().y;
    const Vec3 realFeet{realPos_.x, realPos_.y + yOff, realPos_.z};
    ghostPos_ = smooth::interp(targetAddr_ ^ smooth::kGhostMask, realFeet);
    const Vec3 ghost = ghostPos_;

    CameraView cam = getCameraView(local, game.client());

    const float ex = 0.14f;
    const Vec3 bmin(ghost.x - 0.3f - ex, ghost.y - ex, ghost.z - 0.3f - ex);
    const Vec3 bmax(ghost.x + 0.3f + ex, ghost.y + boxHt + ex, ghost.z + 0.3f + ex);

    Vec3 corners[8];
    int idx = 0;
    for (int yi = 0; yi <= 1; ++yi)
        for (int zi = 0; zi <= 1; ++zi)
            for (int xi = 0; xi <= 1; ++xi)
                corners[idx++] = {bmin.x + (xi ? (bmax.x - bmin.x) : 0.0f),
                                  bmin.y + (yi ? (bmax.y - bmin.y) : 0.0f),
                                  bmin.z + (zi ? (bmax.z - bmin.z) : 0.0f)};

    Vec3 proj[8];
    for (int i = 0; i < 8; ++i)
    {
        if (!cam.project(corners[i], proj[i]))
            return;
    }

    ImDrawList* d = ImGui::GetBackgroundDrawList();
    const ImU32 col = theme::AccentU32(50.0f / 255.0f);
    static const int faces[6][4] = {
        {0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1},
        {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3},
    };
    for (auto& f : faces)
        d->AddQuadFilled(ImVec2(proj[f[0]].x, proj[f[0]].y),
                         ImVec2(proj[f[1]].x, proj[f[1]].y),
                         ImVec2(proj[f[2]].x, proj[f[2]].y),
                         ImVec2(proj[f[3]].x, proj[f[3]].y), col);
}

void Backtrack::drawSettings()
{
    widgets::CSlider("Delay##bt", &delayMs_, 0.0f, 1000.0f, "%.0f ms");
    ImGui::TextDisabled("how far behind the target walks (OpenSlinky: delay)");

    widgets::CSlider("Min distance##bt", &minDist_, 1.0f, 8.0f, "%.1f");
    widgets::CSlider("Max distance##bt", &maxDist_, 1.0f, 8.0f, "%.1f");

    widgets::Checkbox("Disable on hit##bt", &disableOnHit_);
    widgets::Checkbox("Require mouse pressed##bt", &requireMouse_);
    widgets::Checkbox("Render remote position##bt", &renderReal_);
    ImGui::Checkbox("Debug##bt", &debug_);
    ImGui::Separator();

    if (active_ && targetAddr_ != 0)
    {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                           "LAGGING 0x%llX  delay=%.0f ms  dist=%.2f",
                           (unsigned long long)targetAddr_, delayMs_, lastDist_);
        ImGui::TextDisabled("real=(%.2f, %.2f, %.2f)", realPos_.x, realPos_.y, realPos_.z);
        ImGui::TextDisabled("shown=(%.2f, %.2f, %.2f)", shownPos_.x, shownPos_.y,
                            shownPos_.z);
        ImGui::TextDisabled("history=%.0f ms", lgh::oldestAgeMs(targetAddr_));
    }
    else
    {
        ImGui::TextDisabled("idle (%s)", releaseReason_);
        ImGui::TextDisabled("%s", attacking_ ? "attacking" : "not attacking");
    }
}

nlohmann::json Backtrack::saveSettings() const
{
    return nlohmann::json{
        {"delay", delayMs_},
        {"minDist", minDist_},
        {"maxDist", maxDist_},
        {"requireMouse", requireMouse_},
        {"disableOnHit", disableOnHit_},
        {"renderReal", renderReal_},
        {"debug", debug_},
    };
}

void Backtrack::loadSettings(const nlohmann::json& j)
{
    delayMs_ = j.value("delay", delayMs_);
    if (delayMs_ < 0.0f)
        delayMs_ = 0.0f;
    if (delayMs_ > 1000.0f)
        delayMs_ = 1000.0f;
    minDist_ = j.value("minDist", minDist_);
    if (minDist_ < 1.0f)
        minDist_ = 1.0f;
    if (minDist_ > 8.0f)
        minDist_ = 8.0f;
    maxDist_ = j.value("maxDist", maxDist_);
    if (maxDist_ < 1.0f)
        maxDist_ = 1.0f;
    if (maxDist_ > 8.0f)
        maxDist_ = 8.0f;
    requireMouse_ = j.value("requireMouse", requireMouse_);
    disableOnHit_ = j.value("disableOnHit", disableOnHit_);
    renderReal_ = j.value("renderReal", renderReal_);
    debug_ = j.value("debug", debug_);
}

MC_REGISTER_MODULE(Backtrack);

}
