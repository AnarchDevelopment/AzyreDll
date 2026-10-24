#include "Backtrack.hpp"
#include "GUI/Widgets.hpp"

#include "Features/Smoothing.hpp"
#include "Framework/Math.hpp"
#include "Framework/Memory.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Theme.hpp"
#include "SDK/Camera.hpp"
#include "SDK/Game.hpp"
#include "SDK/Offsets.hpp"
#include "SDK/VTables.hpp"

#include <imgui.h>
#include <windows.h>

#include <cmath>

namespace mc {

namespace {

// Cursor del sistema visible = hay una pantalla del juego abierta
// (inventario, contenedor, chat, pause). Mismo chequeo que InventoryWalk.
bool cursorShowing()
{
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (GetCursorInfo(&ci))
        return (ci.flags & CURSOR_SHOWING) != 0;
    return true; // si la consulta falla, asumir UI abierta: no congelar
}

bool finiteVec(const Vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool writeVec3(uintptr_t actor, uintptr_t off, const Vec3& v)
{
    float f[3] = {v.x, v.y, v.z};
    return mem::rawWrite(actor + off, f, sizeof(f));
}

// Reubica el actor en `where` dejando TODA su geometria consistente:
// lee el AABB actual, calcula sus offsets respecto a la posicion real del
// actor y traslada el box intacto. mPosition (+0x0C), el target de red
// del moveTo (+0xD08) y el AABB (+0xB4..+0xC8) quedan alineados — el
// hitbox sigue siendo el box del propio juego, solo que en `where`.
// Devuelve false (sin escribir) si el actor no esta en estado sano.
// `realOut` recibe la posicion logica (mPosition) muestreada.
// `netRealOut` recibe el TARGET DE RED (+0xD08) leido ANTES del rewrite:
// entre la llegada de un MovePlayer/MoveEntity y este write contiene la
// posicion EXACTA del ultimo paquete — el equivalente del acumulado por
// PacketReceiveEvent que hace LegitReach en Rise.
bool placeActorAt(uintptr_t actor, const Vec3& where, Vec3* realOut = nullptr,
                  Vec3* netRealOut = nullptr)
{
    if (!actor || !mem::isReadable(actor, sizeof(uintptr_t)))
        return false;

    // Anti recycling: si la memoria del actor fue liberada y reutilizada
    // por otro objeto, su vtable ya no es la de un player remoto.
    uintptr_t vt = mem::read<uintptr_t>(actor);
    if (vt != mem::resolve(vt::RemotePlayer) &&
        vt != mem::resolve(vt::RemotePlayerAlt))
        return false;

    Vec3 net{};
    if (netRealOut &&
        mem::rawRead(&net, actor + off::player::MoveTargetX, sizeof(float) * 3) &&
        finiteVec(net))
    {
        *netRealOut = net;
    }

    Vec3 real{};
    if (!mem::rawRead(&real, actor + off::player::PosTrueX, sizeof(float) * 3))
        return false;
    if (!finiteVec(real))
        return false;
    if (realOut)
        *realOut = real;

    float box[6] = {};
    if (!mem::rawRead(box, actor + off::player::AabbMinX, sizeof(box)))
        return false;
    for (float f : box)
        if (!std::isfinite(f))
            return false;

    // Box degenerado o absurdo (actor re-spawneando / estado raro):
    // mejor soltar el hold que congelar basura.
    float w = box[3] - box[0];
    float h = box[4] - box[1];
    float d = box[5] - box[2];
    if (w <= 0.0f || h <= 0.0f || d <= 0.0f || w > 10.0f || h > 10.0f || d > 10.0f)
        return false;

    float offMin[3] = {box[0] - real.x, box[1] - real.y, box[2] - real.z};
    float offMax[3] = {box[3] - real.x, box[4] - real.y, box[5] - real.z};

    // AABB primero y en un solo write (24 bytes continuos): el hitbox llega
    // al anchor de una pieza. Si algo falla despues, el box ya es un box
    // valido del juego en `where` y el siguiente paquete real lo sana.
    float moved[6] = {
        where.x + offMin[0], where.y + offMin[1], where.z + offMin[2],
        where.x + offMax[0], where.y + offMax[1], where.z + offMax[2],
    };
    if (!mem::rawWrite(actor + off::player::AabbMinX, moved, sizeof(moved)))
        return false;

    writeVec3(actor, off::player::PosTrueX, where);
    writeVec3(actor, off::player::MoveTargetX, where);
    return true;
}

float dist2(const Vec3& a, const Vec3& b)
{
    Vec3 d = a - b;
    return d.x * d.x + d.y * d.y + d.z * d.z;
}

// ¿El target de red trae un paquete NUEVO? Nuestro freeze reescribe el
// +0xD08 con el anchor cada frame: si difiere del anchor, es porque el
// server mando un MovePlayer/MoveEntity desde el ultimo rewrite — esa
// es la posicion real exacta (el acumulado de paquetes de LegitReach).
bool netIsFresh(const Vec3& net, const Vec3& anchor)
{
    return dist2(net, anchor) > 0.0001f; // >1cm en total
}

} // namespace

Backtrack::Backtrack()
    : Module("Backtrack", "Lag-compensated hits a la Legit Reach de Rise: congela al target en su posicion visible mientras atacas",
             Category::Combat, 0)
{
    markHasSettings();
}

void Backtrack::onEnable()
{
    holdAddr_ = 0;
    holdStart_ = 0;
    hasTarget_ = false;
    lastTargetAddr_ = 0;
    smooth::clear(holdAddr_ ^ smooth::kGhostMask);
}

void Backtrack::releaseHold()
{
    if (holdAddr_ != 0)
    {
        // Dispatch de Rise: un ultimo write consistente con el actor en su
        // posicion REAL (snap-back para el caso "dejo de llegar informacion
        // nueva"). El siguiente paquete del server restaura todo solo.
        placeActorAt(holdAddr_, realPos_);
    }
    holdAddr_ = 0;
    holdStart_ = 0;
}

void Backtrack::onDisable()
{
    releaseHold();
}

void Backtrack::onShutdown()
{
    releaseHold();
}

void Backtrack::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
    {
        holdAddr_ = 0;
        hasTarget_ = false;
        return;
    }

    LocalPlayer local = game.localPlayer();
    Vec3 eye = local.eyePos();
    ULONGLONG now = GetTickCount64();

    // Atacando = LMB fisico + menu del cheat cerrado + sin HUD editor +
    // cursor OCULTO. Las pantallas del juego (inventario incluido) no
    // cuentan como ataques: esto es lo que arrancaba holds — y crashes —
    // en la version anterior.
    bool attacking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 &&
                     !menu::visible() && !menu::hudEditing() &&
                     !cursorShowing();
    lastAttacking_ = attacking;

    // Target: el mas cercano dentro de la ventana de LegitReach.
    const Actor* target = nullptr;
    float targetDist = 0.0f;
    Vec3 targetPos{};

    for (const Actor& other : game.remotePlayers())
    {
        if (!other.valid())
            continue;

        Vec3 p = other.posExact();
        if (!finiteVec(p))
            continue;

        float d = distance3D(eye, p);
        if (d <= maxDist_ && (!target || d < targetDist))
        {
            target = &other;
            targetDist = d;
            targetPos = p;
        }
    }

    // Como Rise (targetEntity != null): mientras haya target en rango la
    // posicion real se trackea viva y el box es visible — sin hold, real ==
    // posicion mostrada (el box calza sobre el player).
    hasTarget_ = target != nullptr;
    if (target)
    {
        lastTargetAddr_ = target->address();
        lastTargetDist_ = targetDist;
        if (holdAddr_ == 0)
            realPos_ = targetPos;
    }

    if (holdAddr_ != 0)
    {
        // --- Hold en curso: validacion + guards de LegitReach ---
        const Actor* held = nullptr;
        for (const Actor& other : game.remotePlayers())
        {
            if (other.address() == holdAddr_ && other.valid())
            {
                held = &other;
                break;
            }
        }

        // Despawn, muerte o memoria reciclada: nada que restaurar, el
        // juego ya tiene al actor en su posicion real.
        if (!held || held->health() <= 0)
        {
            holdAddr_ = 0;
            return;
        }

        // Congelar este tick y capturar el target de red ANTES del rewrite:
        // si trae un paquete nuevo, esa es la posicion REAL exacta (el
        // acumulado por PacketReceiveEvent de LegitReach).
        Vec3 cur{}, net{};
        if (!placeActorAt(holdAddr_, anchor_, &cur, &net))
        {
            holdAddr_ = 0;
            return;
        }
        if (netIsFresh(net, anchor_))
            realPos_ = net;

        // Guard de teleport: salto grande respecto al anchor -> soltar YA
        // y dejar que el siguiente update llegue intacto.
        if (dist2(realPos_, anchor_) > 100.0f)
        {
            releaseHold();
            return;
        }

        float dReal = distance3D(eye, realPos_);
        float dShown = distance3D(eye, anchor_);

        bool expired = (now - holdStart_) > (ULONGLONG)maxPingSpoof_;
        bool tooFar = dReal > maxDist_;
        bool cameCloser = smart_ && (dReal < dShown - 0.15f);

        if (!attacking || expired || tooFar || cameCloser)
        {
            releaseHold();
            return;
        }
    }
    else if (attacking && target)
    {
        // Iniciar hold: el target debe estar dentro de la ventana de
        // LegitReach (2.5..6.0) respecto a su posicion mostrada.
        if (targetDist > minDist_ && targetDist < maxDist_)
        {
            holdAddr_ = target->address();
            anchor_ = targetPos;
            realPos_ = targetPos;
            holdStart_ = now;

            // Primer freeze inmediato; si falla, no hay hold.
            if (!placeActorAt(holdAddr_, anchor_))
            {
                holdAddr_ = 0;
            }
        }
    }
}

void Backtrack::onRender()
{
    Game& game = Game::get();
    if (!game.localFound())
    {
        hasTarget_ = false;
        return;
    }

    // --- Hold en curso: refresh por frame del freeze ---
    if (holdAddr_ != 0)
    {
        // Con cualquier pantalla abierta el refresh corta AL INSTANTE (el
        // cursor aparece antes de que el tick despache). El hold en si lo
        // termina onTick con su dispatch + snap-back.
        if (menu::visible() || menu::hudEditing() || cursorShowing())
            return;

        // Refresh por frame: los paquetes del target llegan a ~20Hz y sin
        // esto el interpolador ganaria hasta 50ms de drift entre ticks. El
        // freeze queda apretado frame a frame — y captura el target de red
        // antes del rewrite para el acumulado paquete-exacto.
        Vec3 cur{}, net{};
        if (placeActorAt(holdAddr_, anchor_, &cur, &net) && netIsFresh(net, anchor_))
            realPos_ = net;

        // Guard de teleport tambien en el refresh: no congelar un anchor
        // contra un real que se fue a otro mapa.
        if (dist2(realPos_, anchor_) > 100.0f)
            return;
    }

    // --- Render Real Location: como LegitReach (Render3DEvent), el box se
    // dibuja siempre que hay target — con hold, en la posicion REAL del
    // server (paquete-exacto); sin hold, calza sobre el player.
    if (!hasTarget_ || !renderReal_ || menu::visible())
        return;

    // Mismo encuadre que el ESP (dos desfases arreglados):
    //   1. Espacio: realPos_ vive en espacio mPosition (+0x0C) = CENTRO
    //      horizontal, que es donde el motor ancla el modelo (cambie el
    //      ESP y la camara a centro; antes todo estaba en +0xB4, offset -0.3
    //      que se cancelaba entre si pero desfasaba los bloques). Solo hay
    //      que mapear la Y a pies (+0xB4.y): yOff = pos().y - posExact().y.
    //      Alto = height() (+0xD4) en vez de 1.8 fijo.
    //   2. Tiempo: smooth::value con delay de 150ms -> la caja llegaba
    //      SIEMPRE 150ms tarde respecto al modelo. Ahora interp (hand-off
    //      seamless, QPC, sin delay) = el ESP.
    uintptr_t shown = holdAddr_ ? holdAddr_ : lastTargetAddr_;
    Actor shownActor(shown);
    float yOff = 0.0f;
    float boxHt = 1.8f;
    if (shownActor.valid())
    {
        yOff = shownActor.pos().y - shownActor.posExact().y;
        float h = shownActor.height();
        if (h > 0.1f)
            boxHt = h;
    }
    Vec3 realFeet{realPos_.x, realPos_.y + yOff, realPos_.z};
    ghostPos_ = smooth::interp(shown ^ smooth::kGhostMask, realFeet);
    Vec3 ghost = ghostPos_;

    LocalPlayer local = game.localPlayer();
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;

    CameraView cam = getCameraView(local, game.client());

    // Box estilo LegitReach: bounding del player rebaseado al origen y
    // offseteado a la posicion animada, expand(0.14) en todos los ejes —
    // bottom en los pies de la posicion real (mismo frame que el ESP).
    float ex = 0.14f;
    Vec3 bmin(ghost.x - 0.3f - ex, ghost.y - ex, ghost.z - 0.3f - ex);
    Vec3 bmax(ghost.x + 0.3f + ex, ghost.y + boxHt + ex, ghost.z + 0.3f + ex);

    Vec3 corners[8];
    int idx = 0;
    for (int yi = 0; yi <= 1; ++yi)
        for (int zi = 0; zi <= 1; ++zi)
            for (int xi = 0; xi <= 1; ++xi)
                corners[idx++] = {bmin.x + (xi ? (bmax.x - bmin.x) : 0.0f),
                                  bmin.y + (yi ? (bmax.y - bmin.y) : 0.0f),
                                  bmin.z + (zi ? (bmax.z - bmin.z) : 0.0f)};

    Vec3 proj[8];
    bool allVisible = true;
    for (int i = 0; i < 8; ++i)
    {
        if (!worldToScreen(corners[i], cam.pos, cam.yaw, cam.pitch,
                           readGameFov(), sw, sh, proj[i]))
        {
            allVisible = false;
            break;
        }
    }
    if (!allVisible)
        return;

    // LegitReach renderiza el box con GL_QUADS (glBegin(7)): 6 caras
    // FILLED con ColorUtil.withAlpha(color, 50) — no wireframe. Puerto a
    // la proyeccion 2D: un quad relleno por cara (sin culling ni sorting,
    // igual que el GL sin depth-write: las caras translucidas se
    // acumulan solas y el box gana presencia).
    ImDrawList* d = ImGui::GetBackgroundDrawList();
    ImU32 col = theme::AccentU32(50.0f / 255.0f);
    static const int faces[6][4] = {
        {0, 1, 3, 2}, // bottom (ymin)
        {4, 6, 7, 5}, // top (ymax)
        {0, 4, 5, 1}, // zmin
        {2, 3, 7, 6}, // zmax
        {0, 2, 6, 4}, // xmin
        {1, 5, 7, 3}, // xmax
    };
    for (auto& f : faces)
        d->AddQuadFilled(ImVec2(proj[f[0]].x, proj[f[0]].y),
                         ImVec2(proj[f[1]].x, proj[f[1]].y),
                         ImVec2(proj[f[2]].x, proj[f[2]].y),
                         ImVec2(proj[f[3]].x, proj[f[3]].y), col);
}

void Backtrack::drawSettings()
{
    widgets::CSlider("Max Ping Spoof##bt", &maxPingSpoof_, 50.0f, 5000.0f, "%.0f ms");
    ImGui::TextDisabled("max hold: despues despacha (Rise: 1000ms)");
    widgets::CSlider("Min distance##bt", &minDist_, 1.0f, 6.0f, "%.1f");
    widgets::CSlider("Max distance##bt", &maxDist_, 2.0f, 8.0f, "%.1f");
    widgets::Checkbox("Smart##bt", &smart_);
    ImGui::SameLine();
    ImGui::TextDisabled("(suelta si la real esta mas cerca)");
    widgets::Checkbox("Render Real Location##bt", &renderReal_);
    ImGui::Separator();

    // --- Diagnostico en vivo (para verificar el pipeline) ---
    ImGui::TextDisabled("mode: memory freeze (no hooks, no vtable patches)");

    if (holdAddr_ != 0)
    {
        ULONGLONG now = GetTickCount64();
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                            "HOLDING 0x%llX  (%llu ms / %.0f)",
                            (unsigned long long)holdAddr_,
                            now > holdStart_ ? now - holdStart_ : 0,
                            maxPingSpoof_);
        ImGui::TextDisabled("anchor=(%.1f, %.1f, %.1f)",
                            anchor_.x, anchor_.y, anchor_.z);
        ImGui::TextDisabled("real=(%.1f, %.1f, %.1f)",
                            realPos_.x, realPos_.y, realPos_.z);
    }
    else if (lastTargetAddr_)
    {
        ImGui::TextDisabled("target: 0x%llX  dist=%.2f (need %.1f..%.1f%s)",
                            (unsigned long long)lastTargetAddr_,
                            lastTargetDist_, minDist_, maxDist_,
                            lastAttacking_ ? " while attacking" : "");
    }
    else
    {
        ImGui::TextDisabled("target: none in range");
    }
}

nlohmann::json Backtrack::saveSettings() const
{
    return nlohmann::json{
        {"pingSpoof", maxPingSpoof_},
        {"minDist", minDist_},
        {"maxDist", maxDist_},
        {"smart", smart_},
        {"renderReal", renderReal_},
    };
}

void Backtrack::loadSettings(const nlohmann::json& j)
{
    maxPingSpoof_ = j.value("pingSpoof", maxPingSpoof_);
    if (maxPingSpoof_ < 50.0f)
        maxPingSpoof_ = 50.0f;
    if (maxPingSpoof_ > 5000.0f)
        maxPingSpoof_ = 5000.0f;
    minDist_ = j.value("minDist", minDist_);
    if (minDist_ < 1.0f)
        minDist_ = 1.0f;
    if (minDist_ > 6.0f)
        minDist_ = 6.0f;
    maxDist_ = j.value("maxDist", maxDist_);
    if (maxDist_ < 2.0f)
        maxDist_ = 2.0f;
    if (maxDist_ > 8.0f)
        maxDist_ = 8.0f;
    smart_ = j.value("smart", smart_);
    renderReal_ = j.value("renderReal", renderReal_);
}

MC_REGISTER_MODULE(Backtrack);

}
