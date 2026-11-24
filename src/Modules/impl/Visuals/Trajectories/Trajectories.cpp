#include "Trajectories.hpp"
#include "GUI/Widgets.hpp"

#include "Features/Raycast.hpp"
#include "Framework/Math.hpp"
#include "SDK/Client/Camera.hpp"
#include "SDK/Core/Game.hpp"

#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <vector>

namespace mc {

namespace {

Vec3 viewDir(const LocalPlayer& lp)
{
    float yaw = lp.yaw();
    float pitch = lp.pitch();
    return {
        -std::sin(deg2rad(yaw)) * std::cos(deg2rad(pitch)),
        -std::sin(deg2rad(pitch)),
        std::cos(deg2rad(yaw)) * std::cos(deg2rad(pitch)),
    };
}

// Ids clasicos (Java/Bedrock legacy) de lanzables; si el build usa otros,
// basta con ajustar la tabla. Con autoItem_ desactivado se usan los sliders.
// kind: 0 = velocidad fija, 1 = escala con la carga del arco.
struct ItemTag
{
    int first;
    int last;
    float speed;
    float gravity;
    float drag;
    int kind;
};
const ItemTag kTags[] = {
    {344, 344, 1.5f, 0.03f, 0.99f, 0},  // egg
    {332, 332, 1.5f, 0.03f, 0.99f, 0},  // snowball
    {368, 368, 1.5f, 0.03f, 0.99f, 0},  // ender pearl
    {373, 373, 0.5f, 0.03f, 0.99f, 0},  // splash potion
    {378, 378, 0.5f, 0.03f, 0.99f, 0},  // lingering potion
    {261, 261, 3.0f, 0.05f, 0.99f, 1},  // bow (escala con la carga)
    {942, 942, 3.2f, 0.05f, 0.99f, 1},  // crossbow
    {836, 836, 2.5f, 0.05f, 0.99f, 0},  // trident
    {346, 346, 1.5f, 0.04f, 0.99f, 0},  // fishing rod
    {384, 384, 0.75f, 0.04f, 0.99f, 0}, // bottle o' enchanting
};

// Test segmento (a -> b) contra un AABB. Devuelve true y el t del primer
// contacto (0..1) para saber donde entra la curva.
bool segmentHitsAabb(const Vec3& a, const Vec3& b, const Vec3& mn, const Vec3& mx, float& tHit)
{
    const float dir[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
    const float origin[3] = {a.x, a.y, a.z};
    const float lo[3] = {mn.x, mn.y, mn.z};
    const float hi[3] = {mx.x, mx.y, mx.z};

    float tMin = 0.0f;
    float tMax = 1.0f;
    for (int i = 0; i < 3; ++i)
    {
        if (std::fabs(dir[i]) < 1e-6f)
        {
            if (origin[i] < lo[i] || origin[i] > hi[i])
                return false;
            continue;
        }
        float t1 = (lo[i] - origin[i]) / dir[i];
        float t2 = (hi[i] - origin[i]) / dir[i];
        if (t1 > t2)
        {
            const float tmp = t1;
            t1 = t2;
            t2 = tmp;
        }
        if (t1 > tMin)
            tMin = t1;
        if (t2 < tMax)
            tMax = t2;
        if (tMin > tMax)
            return false;
    }
    tHit = tMin;
    return true;
}

} // namespace

Trajectories::Trajectories()
    : Module("Trajectories", "Predicts the arc of the thrown/held projectile",
             Category::Visuals, 0)
{
    markHasSettings();
}

bool Trajectories::itemParams(const ItemInstance& held)
{
    const int id = held.itemId();
    if (id < 0)
        return false;
    for (const ItemTag& t : kTags)
    {
        if (id >= t.first && id <= t.last)
        {
            speed_ = t.speed;
            gravity_ = t.gravity;
            drag_ = t.drag;
            if (t.kind == 1)
            {
                // La velocidad de salida del arco va de ~1 b/t (carga 0)
                // hasta el maximo del item cuando esta cargado.
                const float charge = clampf(bowCharge_, 0.0f, 1.0f);
                speed_ = 1.0f + (t.speed - 1.0f) * charge;
                gravity_ = t.gravity * (0.6f + 0.4f * charge);
            }
            return true;
        }
    }
    return false;
}

void Trajectories::simulate(const LocalPlayer& lp, const BlockSource& bs, SimResult& out)
{
    out.points.clear();
    out.landingValid = false;
    out.entityHit = false;
    out.distance = 0.0f;
    out.flightTime = 0.0f;

    // El proyectil sale del ojo y no del centro del AABB: con el origen en
    // eyePos puro la curva se desvia en tiros horizontales largos.
    const Vec3 dir = normalize(viewDir(lp));
    Vec3 pos = lp.eyePos() + dir * 0.1f;
    const Vec3 origin = pos;
    Vec3 vel = dir * speed_;

    float ticks = maxTime_ * (float)off::gameconst::Tps;
    if (ticks > 400.0f)
        ticks = 400.0f;

    // AABB de los jugadores visibles: corta el arco en el primero alcanzado.
    struct Box
    {
        Vec3 mn;
        Vec3 mx;
    };
    std::vector<Box> boxes;
    if (hitEntities_)
    {
        for (const Actor& a : Game::get().remotePlayers())
        {
            if (!a.valid())
                continue;
            const Vec3 center = a.posExact();
            const Vec3 feet = a.pos();
            const float half = 0.3f;
            boxes.push_back({Vec3{center.x - half, feet.y, center.z - half},
                             Vec3{center.x + half, feet.y + a.height(), center.z + half}});
        }
    }

    int bounces = 0;
    for (int i = 0; i < (int)ticks; ++i)
    {
        out.flightTime = (float)i / (float)off::gameconst::Tps;
        if (i % 2 == 0) // muestrear a media resolucion para curvas largas
            out.points.push_back(pos);

        vel.y -= gravity_;
        vel = vel * drag_;

        float step = vecLength(vel);
        if (step > 4.5f) // evita tunneling en saltos grandes
            return;

        const Vec3 next = pos + vel;

        float entityT = 2.0f;
        if (hitEntities_)
        {
            for (const Box& b : boxes)
            {
                float t = 0.0f;
                if (segmentHitsAabb(pos, next, b.mn, b.mx, t) && t < entityT)
                    entityT = t;
            }
        }

        RaycastHit h = raycastBlock(bs, pos, vel, step);
        if (entityT <= 1.0f && (!h.hit || entityT * step <= h.distance))
        {
            out.landing = pos + vel * entityT;
            out.landingValid = true;
            out.entityHit = true;
            out.points.push_back(out.landing);
            out.distance = distance3D(origin, out.landing);
            return;
        }

        if (h.hit)
        {
            out.landing = h.point;
            out.landingValid = true;
            out.distance = distance3D(origin, h.point);
            out.points.push_back(h.point);

            if (bounce_ && bounces < 3 && h.face >= 0)
            {
                // Reflejar sobre la normal de la cara golpeada y perder energia.
                Vec3 n{0.0f, 0.0f, 0.0f};
                switch (h.face)
                {
                case 0: n = {0.0f, 1.0f, 0.0f}; break;  // abajo
                case 1: n = {0.0f, -1.0f, 0.0f}; break; // arriba
                case 2: n = {0.0f, 0.0f, 1.0f}; break;  // norte (-z)
                case 3: n = {0.0f, 0.0f, -1.0f}; break; // sur (+z)
                case 4: n = {1.0f, 0.0f, 0.0f}; break;  // oeste (-x)
                case 5: n = {-1.0f, 0.0f, 0.0f}; break; // este (+x)
                default: break;
                }
                vel = (vel - n * (2.0f * dot(vel, n))) * 0.5f;
                pos = h.point + n * 0.01f;
                out.landingValid = false;
                ++bounces;
                continue;
            }
            return;
        }

        pos = next;
        if (pos.y < -64.0f)
            break;
    }
}

void Trajectories::onRender()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;
    LocalPlayer lp = game.localPlayer();
    if (!lp.valid())
        return;

    ItemInstance held = lp.heldItem();
    if (!held.valid() || held.count() <= 0 || held.isBlockItem())
        return; // bloque/mano vacia: nada que lanzar

    if (autoItem_ && !itemParams(held))
        return; // item en mano no lanzable: no hay trayectoria que predecir

    BlockSource bs = game.blockSource();
    if (!bs.valid())
        return;

    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 0.0f || sh <= 0.0f)
        return;

    CameraView cam = getCameraView(lp, game.client());
    ImDrawList* d = ImGui::GetForegroundDrawList();
    const ImU32 col = ImGui::ColorConvertFloat4ToU32(color_);

    SimResult sim;
    simulate(lp, bs, sim);

    const std::vector<Vec3>& pts = sim.points;
    if (pts.size() < 2)
        return;

    // Descartar puntos detras de la camara si la curva deja la vista.
    std::vector<ImVec2> screen;
    screen.reserve(pts.size());
    for (const Vec3& wp : pts)
    {
        Vec3 s;
        if (!cam.project(wp, s))
        {
            if (!screen.empty())
                break; // la curva escapo por detras: cortar
            continue;
        }
        screen.push_back(ImVec2(s.x, s.y));
    }
    if (screen.size() >= 2)
    {
        d->AddPolyline(screen.data(), (int)screen.size(), col, 0, thickness_);
        d->AddCircleFilled(screen.front(), 2.5f, col, 12);
    }

    if (drawLanding_ && sim.landingValid)
    {
        Vec3 s;
        if (cam.project(sim.landing, s))
        {
            ImVec2 sp(s.x, s.y);
            // Impacto en entidad: marcador relleno y en rojo para distinguirlo
            // del impacto en bloque.
            const ImU32 marker = sim.entityHit
                ? IM_COL32(255, 80, 80, 220)
                : col;
            if (sim.entityHit)
                d->AddCircleFilled(sp, 4.0f, marker, 20);
            d->AddCircle(sp, 5.0f, marker, 24, thickness_);
            d->AddLine(ImVec2(sp.x - 3.0f, sp.y), ImVec2(sp.x + 3.0f, sp.y), marker, thickness_);
            d->AddLine(ImVec2(sp.x, sp.y - 3.0f), ImVec2(sp.x, sp.y + 3.0f), marker, thickness_);

            if (showInfo_)
            {
                char info[64] = {};
                snprintf(info, sizeof(info), "%.1f b  %.2f s", sim.distance, sim.flightTime);
                d->AddText(ImVec2(sp.x + 9.0f, sp.y - 6.0f), marker, info);
            }
        }
    }
}

void Trajectories::drawSettings()
{
    widgets::Checkbox("Auto per item##tj", &autoItem_);
    if (!autoItem_)
    {
        ImGui::Indent(14.0f);
        widgets::CSlider("Speed##tj", &speed_, 0.3f, 5.0f, "%.2f");
        widgets::CSlider("Gravity##tj", &gravity_, 0.0f, 0.15f, "%.3f");
        widgets::CSlider("Drag##tj", &drag_, 0.90f, 1.0f, "%.3f");
        ImGui::Unindent(14.0f);
    }
    else
    {
        ImGui::Indent(14.0f);
        widgets::CSlider("Bow charge##tj", &bowCharge_, 0.0f, 1.0f, "%.2f");
        ImGui::Unindent(14.0f);
    }
    widgets::CSlider("Max time (s)##tj", &maxTime_, 0.5f, 5.0f, "%.1f");
    widgets::Checkbox("Landing marker##tj", &drawLanding_);
    widgets::Checkbox("Show flight info##tj", &showInfo_);
    widgets::Checkbox("Hit players##tj", &hitEntities_);
    widgets::Checkbox("Bounce off blocks##tj", &bounce_);
    widgets::CSlider("Thickness##tj", &thickness_, 0.5f, 4.0f, "%.1f");
    widgets::ColorEdit4("Color##tj", (float*)&color_);
}

MC_REGISTER_MODULE(Trajectories);

}