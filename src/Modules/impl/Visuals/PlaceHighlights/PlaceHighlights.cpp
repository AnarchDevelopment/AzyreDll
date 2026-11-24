#include "PlaceHighlights.hpp"
#include "Features/ECS/ECS.hpp"
#include "Framework/ColorUtils.hpp"
#include "Framework/Hook.hpp"
#include "Framework/Log.hpp"
#include "Framework/Math.hpp"
#include "Framework/Memory.hpp"
#include "GUI/Widgets.hpp"
#include "SDK/Client/Camera.hpp"
#include "SDK/Core/Game.hpp"

#include <imgui.h>
#include <MinHook.h>
#include <windows.h>

#include <cmath>
#include <cstring>

namespace mc {

namespace {

// Puerto de MathUtils::getBoxPoints/getImBoxPoints (Solstice): proyecta las
// 8 esquinas del AABB y devuelve el convex hull en orden (gift wrapping
// desde el punto mas a la izquierda). Si cualquier esquina cae detras de
// la camara devuelve vacio (el bloque no se dibuja, tampoco se remueve).
bool getBoxPoints(const Vec3& bmin, const Vec3& bmax, const CameraView& cam,
                  std::vector<ImVec2>& out)
{
    const Vec3 corners[8] = {
        {bmin.x, bmin.y, bmin.z}, {bmin.x, bmin.y, bmax.z},
        {bmax.x, bmin.y, bmin.z}, {bmax.x, bmin.y, bmax.z},
        {bmin.x, bmax.y, bmin.z}, {bmin.x, bmax.y, bmax.z},
        {bmax.x, bmax.y, bmin.z}, {bmax.x, bmax.y, bmax.z},
    };

    std::vector<ImVec2> pts;
    pts.reserve(8);
    for (const Vec3& wp : corners)
    {
        Vec3 proj;
        if (!cam.project(wp, proj))
            return false;
        pts.push_back(ImVec2(proj.x, proj.y));
    }
    if (pts.size() < 3)
        return false;

    constexpr float kPi = 3.14159265358979323846f;

    // Punto mas a la izquierda = arranque del hull.
    ImVec2 start = pts[0];
    for (const ImVec2& p : pts)
        if (p.x < start.x)
            start = p;

    out.clear();
    out.push_back(start);
    ImVec2 current = start;
    ImVec2 lastDir(0.0f, -1.0f);

    do
    {
        float smallestAngle = 2.0f * kPi;
        ImVec2 smallestDir(0.0f, 0.0f);
        ImVec2 smallestE(0.0f, 0.0f);
        float lastAtan = atan2f(lastDir.y, lastDir.x);

        for (const ImVec2& t : pts)
        {
            if (t.x == current.x && t.y == current.y)
                continue;
            ImVec2 dir(t.x - current.x, t.y - current.y);
            float angle = atan2f(dir.y, dir.x) - lastAtan;
            if (angle > kPi)
                angle -= 2.0f * kPi;
            else if (angle <= -kPi)
                angle += 2.0f * kPi;
            if (angle >= 0.0f && angle < smallestAngle)
            {
                smallestAngle = angle;
                smallestDir = dir;
                smallestE = t;
            }
        }

        out.push_back(smallestE);
        lastDir = smallestDir;
        current = smallestE;
    } while ((current.x != start.x || current.y != start.y) && out.size() < 9);

    return true;
}

} // namespace

// =====================================================================
// Hook sobre ItemStack::useOn dispatcher (sub_14055D960):
// (itemStack*, player*, x, y, z, face, clickRel.x, clickRel.y, clickRel.z).
// Es la ruta común usada por GameMode::useItemOn después del envío del
// paquete, y también por la predicción local de Scaffold en Survival.
// =====================================================================

using ItemStackUseOnFn = uintptr_t(__fastcall*)(void*, void*, uint32_t, int, int,
                                                 char, float, float, float);
ItemStackUseOnFn oItemStackUseOn = nullptr;
void* g_hookTarget = nullptr;
bool g_hookInstalled = false;
PlaceHighlights* g_instance = nullptr;

static uintptr_t __fastcall hkItemStackUseOn(void* itemInstance, void* player,
                                             uint32_t x, int y, int z, char face,
                                             float clickX, float clickY, float clickZ)
{
    // Preserve the game's original result and exception behavior. Swallowing
    // an exception here made a failed/aborted native placement look like a
    // normal false return and could leave callers' placement state out of sync.
    const uintptr_t result = oItemStackUseOn(itemInstance, player, x, y, z, face,
                                              clickX, clickY, clickZ);

    if (result && g_instance && face >= 0 && face <= 5)
    {
        // Telemetry runs only after the native placement has completed. Keep
        // any failure in the optional visual/ECS bookkeeping from affecting
        // the original placement result.
        __try
        {
            int px = static_cast<int>(x), py = y, pz = z;
            if (face == 0) py -= 1;
            else if (face == 1) py += 1;
            else if (face == 2) pz -= 1;
            else if (face == 3) pz += 1;
            else if (face == 4) px -= 1;
            else if (face == 5) px += 1;

            int id = -1;
            BlockSource bs = Game::get().blockSource();
            if (bs.valid())
                id = bs.getBlockId(px, py, pz);
            ecs::get().reportBlockPlaced({px, py, pz}, id);
            g_instance->addPlacedBlock(static_cast<int>(x), y, z, face);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            // Rendering telemetry is best-effort; never change native place.
        }
    }

    return result;
}

bool installPlaceHook()
{
    if (g_hookInstalled)
        return true;
    if (!hook::initialize())
        return false;

    void* target = (void*)mem::resolve(off::fn::ItemStackUseOn);
    if (!target)
        return false;

    if (!hook::create(target, reinterpret_cast<void*>(&hkItemStackUseOn),
                      reinterpret_cast<void**>(&oItemStackUseOn)))
        return false;

    g_hookTarget = target;
    g_hookInstalled = true;
    return true;
}

void removePlaceHook()
{
    if (g_hookTarget)
    {
        MH_DisableHook(g_hookTarget);
        g_hookTarget = nullptr;
    }
    g_hookInstalled = false;
    oItemStackUseOn = nullptr;
}

PlaceHighlights::PlaceHighlights()
    : Module("PlaceHighlights", "Fading highlight on blocks you place.", Category::Visuals, 0)
{
    markHasSettings();
}

void PlaceHighlights::onEnable()
{
    g_instance = this;
    if (!installPlaceHook())
        MC_LOG_ERROR("[PlaceHighlights] Hook install failed");
}

void PlaceHighlights::onDisable()
{
    if (g_instance == this)
        g_instance = nullptr;
    removePlaceHook();
    std::lock_guard<std::mutex> lock(mapMutex_);
    placedBlocks_.clear();
}

void PlaceHighlights::onShutdown()
{
    if (g_instance == this)
        g_instance = nullptr;
    removePlaceHook();
}

void PlaceHighlights::addPlacedBlock(int x, int y, int z, int face)
{
    std::lock_guard<std::mutex> lock(mapMutex_);
    placedBlocks_[{x, y, z, face}] = GetTickCount64();
}

void PlaceHighlights::onRender()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    if (!local.valid())
        return;

    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 1.0f || sh <= 1.0f)
        return;

    // getCameraView: camara compartida (incluye el offset del renderer y el
    // zoom), la misma que usan ESP/NameTags/Tracers.
    CameraView cam = getCameraView(local, game.client());
    ImDrawList* d = ImGui::GetBackgroundDrawList();
    ULONGLONG now = GetTickCount64();

    std::lock_guard<std::mutex> lock(mapMutex_);
    if (placedBlocks_.empty())
        return;

    std::vector<std::tuple<int, int, int, int>> expired;

    for (auto& [pos, placedTime] : placedBlocks_)
    {
        // Fade exacto del original: alpha = maxOpacity - elapsed/duration,
        // removido cuando (int)(alpha * 200) < 0.
        float elapsed = (float)(now - placedTime);
        float alpha = maxOpacity_ - (elapsed / duration_);
        int alphaInt = (int)(alpha * 200.0f);
        if (alphaInt < 0)
        {
            expired.push_back(pos);
            continue;
        }

        auto [bx, by, bz, face] = pos;

        // Posicion del bloque colocado: raw del hook + offset por cara (el
        // mOffsetList de Solstice).
        float offX2 = 0.0f, offY2 = 0.0f, offZ2 = 0.0f;
        if (face == 0) offY2 = -1.0f;
        else if (face == 1) offY2 = 1.0f;
        else if (face == 2) offZ2 = -1.0f;
        else if (face == 3) offZ2 = 1.0f;
        else if (face == 4) offX2 = -1.0f;
        else if (face == 5) offX2 = 1.0f;

        Vec3 bmin((float)bx + offX2, (float)by + offY2, (float)bz + offZ2);
        Vec3 bmax(bmin.x + 1.0f, bmin.y + 1.0f, bmin.z + 1.0f);

        std::vector<ImVec2> hull;
        if (!getBoxPoints(bmin, bmax, cam, hull))
            continue;

        ImU32 cColor;
        if (colorMode_ == 1)
        {
            cColor = colors::getThemedColor(0.0f, alpha);
        }
        else
        {
            cColor = ImGui::ColorConvertFloat4ToU32(
                ImVec4(color_.x, color_.y, color_.z, alpha));
        }

        if (filled_)
            d->AddConvexPolyFilled(hull.data(), (int)hull.size(), cColor);
        d->AddPolyline(hull.data(), (int)hull.size(), cColor, 2.0f, 0);
    }

    for (auto& pos : expired)
        placedBlocks_.erase(pos);
}

void PlaceHighlights::drawSettings()
{
    const char* modes[] = {"Custom", "Theme"};
    widgets::CCombo("Color Mode##ph", &colorMode_, modes, 2);
    if (colorMode_ == 0)
        widgets::ColorEdit4("Box Color##ph", (float*)&color_);

    widgets::CSlider("Duration (ms)##ph", &duration_, 500.0f, 5000.0f, "%.0f");
    widgets::CSlider("Max opacity##ph", &maxOpacity_, 0.2f, 1.0f, "%.2f");
    widgets::Checkbox("Filled##ph", &filled_);
}

MC_REGISTER_MODULE(PlaceHighlights);

}
