#include "Trajectories.hpp"
#include "GUI/Widgets.hpp"

#include "Features/Raycast.hpp"
#include "Framework/Math.hpp"
#include "SDK/Camera.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <cmath>

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
struct ItemTag
{
    int first;
    int last;
    float speed;
    float gravity;
};
const ItemTag kTags[] = {
    {344, 344, 1.5f, 0.03f},        // egg
    {332, 332, 1.5f, 0.03f},        // snowball
    {368, 368, 1.5f, 0.03f},        // ender pearl
    {373, 373, 0.5f, 0.03f},        // splash potion
    {378, 378, 0.5f, 0.03f},        // lingering potion
    {261, 261, 3.0f, 0.05f},        // bow
    {942, 942, 3.2f, 0.05f},        // crossbow (id variable segun linea)
    {836, 836, 2.5f, 0.05f},        // trident
    {346, 346, 1.5f, 0.04f},        // fishing rod
    {384, 384, 0.75f, 0.04f},       // bottle o' enchanting
};

} // namespace

Trajectories::Trajectories()
    : Module("Trajectories", "Predicts the arc of the thrown/held projectile",
             Category::Visuals, 0)
{
    markHasSettings();
}

void Trajectories::itemParams(const ItemInstance& held)
{
    int id = held.itemId();
    if (id < 0)
        return;
    for (const ItemTag& t : kTags)
    {
        if (id >= t.first && id <= t.last)
        {
            speed_ = t.speed;
            gravity_ = t.gravity;
            drag_ = 0.99f;
            return;
        }
    }
}

void Trajectories::simulate(const LocalPlayer& lp, const BlockSource& bs,
                            std::vector<Vec3>& pts, bool& landingValid, Vec3& landing)
{
    pts.clear();
    landingValid = false;

    Vec3 pos = lp.eyePos();
    Vec3 vel = viewDir(lp) * speed_;
    float ticks = maxTime_ * off::gameconst::Tps;
    if (ticks > 400.0f)
        ticks = 400.0f;

    for (int i = 0; i < (int)ticks; ++i)
    {
        if (i % 2 == 0) // muestrear a media resolucion para curvas largas
            pts.push_back(pos);

        vel.y -= gravity_;
        vel = vel * drag_;

        float step = vecLength(vel);
        if (step > 4.5f) // evita tunneling en saltos grandes
            return;

        RaycastHit h = raycastBlock(bs, pos, vel, step);
        if (h.hit)
        {
            landing = h.point;
            landingValid = true;
            pts.push_back(landing);
            break;
        }

        pos = pos + vel;
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

    if (autoItem_)
        itemParams(held);

    BlockSource bs = game.blockSource();
    if (!bs.valid())
        return;

    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 0.0f || sh <= 0.0f)
        return;

    if (!fovInited_)
    {
        fov_ = readGameFov();
        fovInited_ = true;
    }

    CameraView cam = getCameraView(lp, game.client());
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImU32 col = ImGui::ColorConvertFloat4ToU32(color_);

    std::vector<Vec3> pts;
    bool landingValid = false;
    Vec3 landing;
    simulate(lp, bs, pts, landingValid, landing);

    if (pts.size() < 2)
        return;

    // Descartar puntos detras de la camara si la curva deja la vista.
    std::vector<ImVec2> screen;
    screen.reserve(pts.size());
    for (const Vec3& wp : pts)
    {
        Vec3 s;
        if (!worldToScreen(wp, cam.pos, cam.yaw, cam.pitch, fov_, sw, sh, s))
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

    if (drawLanding_ && landingValid)
    {
        Vec3 s;
        if (worldToScreen(landing, cam.pos, cam.yaw, cam.pitch, fov_, sw, sh, s))
        {
            ImVec2 sp(s.x, s.y);
            const ImU32 r = col;
            d->AddCircle(sp, 5.0f, r, 24, thickness_);
            d->AddLine(ImVec2(sp.x - 3.0f, sp.y), ImVec2(sp.x + 3.0f, sp.y), r, thickness_);
            d->AddLine(ImVec2(sp.x, sp.y - 3.0f), ImVec2(sp.x, sp.y + 3.0f), r, thickness_);
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
    widgets::CSlider("Max time (s)##tj", &maxTime_, 0.5f, 5.0f, "%.1f");
    widgets::Checkbox("Landing marker##tj", &drawLanding_);
    widgets::CSlider("Thickness##tj", &thickness_, 0.5f, 4.0f, "%.1f");
    widgets::ColorEdit4("Color##tj", (float*)&color_);
}

MC_REGISTER_MODULE(Trajectories);

}