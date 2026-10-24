#include "Step.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "Framework/Memory.hpp"
#include "GUI/Menu.hpp"
#include "SDK/ChatHook.hpp"
#include "SDK/Classes/BlockSource.hpp"
#include "SDK/Classes/LocalPlayer.hpp"
#include "SDK/Game.hpp"

#include <windows.h>
#include <imgui.h>

#include <cmath>
#include <climits>
#include <cstdio>

namespace mc {

namespace {

bool keyDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool isCursorShowing()
{
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (GetCursorInfo(&ci))
        return (ci.flags & CURSOR_SHOWING) != 0;
    return false;
}

float axisOf(int negVk, int posVk)
{
    float v = 0.0f;
    if (GetAsyncKeyState(posVk) & 0x8000)
        v += 1.0f;
    if (GetAsyncKeyState(negVk) & 0x8000)
        v -= 1.0f;
    return v;
}

// Legacy block ids: static variants of water/lava behave as the flowing ones.
bool isLiquidId(int id)
{
    return id == 8 || id == 9 || id == 10 || id == 11;
}

// Solid for Step purposes: id > 0 and not liquid. NOTA de seguridad: id == -1
// (lectura fallida) NO es solido — se descarta el step entero en vez de asumir.
bool isSolidId(int id)
{
    return id > 0 && !isLiquidId(id);
}

} // namespace

Step::Step()
    : Module("Step", "Automatically steps up 1-2 block walls without jumping",
             Category::Movement, 0)
{
    markHasSettings();
}

void Step::onEnable()
{
    lastStepMs_ = 0;
    stepTargetY_ = 0.0f;
    stepping_ = false;
}

void Step::displayDebug(const std::string& msg)
{
    if (!debug_)
        return;
    chathook::display("§7[§bStep§7]§f " + msg);
}

void Step::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
    {
        stepping_ = false;
        return;
    }

    LocalPlayer local = game.localPlayer();
    if (!local.valid())
    {
        stepping_ = false;
        return;
    }

    BlockSource bs = game.blockSource();
    if (!bs.valid())
    {
        stepping_ = false;
        return;
    }

    tickStep(local, bs);
}

void Step::tickStep(LocalPlayer& local, const BlockSource& bs)
{
    // --- Gates: menus, sneak, sane state -----------------------------------
    if (menu::visible() || isCursorShowing())
    {
        stepping_ = false;
        return;
    }

    Vec3 feet = local.pos();     // AABB min: los pies (+0xB4)
    Vec3 exact = local.posExact(); // mPosition (+0x0C): centro horizontal
    Vec3 vel = local.velocity();

    bool sane = std::isfinite(feet.x) && std::isfinite(feet.y) && std::isfinite(feet.z) &&
                std::isfinite(exact.x) && std::isfinite(exact.z) &&
                std::isfinite(vel.x) && std::isfinite(vel.y) && std::isfinite(vel.z);
    if (!sane)
    {
        stepping_ = false;
        return;
    }

    bool moving = keyDown('W') || keyDown('A') || keyDown('S') || keyDown('D');
    if (!moving)
    {
        stepping_ = false;
        return;
    }

    // Sneaking nunca hace auto-step (vanilla tampoco).
    if (keyDown(VK_SHIFT))
    {
        stepping_ = false;
        return;
    }

    // En liquidos el vanilla maneja el movimiento: fuera.
    int feetCellY = (int)std::floor(feet.y + 0.01f);
    int ownId = bs.getBlockId((int)std::floor(exact.x), feetCellY, (int)std::floor(exact.z));
    if (isLiquidId(ownId))
    {
        stepping_ = false;
        return;
    }

    // --- Modo Motion: continuar un step ya en progreso ---------------------
    // (sin gates extra: mientras los pies esten por debajo del target y el
    // jugador siga apretando, la velocidad vertical se mantendra).
    if (stepping_ && mode_ == kMotion)
    {
        if (feet.y < stepTargetY_ - 0.05f)
        {
            Vec3 v = local.velocity();
            v.y = liftSpeed_;
            local.setVelocity(v);
            return;
        }
        // Target alcanzado: la gravedad asienta al jugador sobre el bloque.
        stepping_ = false;
    }

    // --- Deteccion de "bloqueado contra una pared": -------------------------
    //  onGround + vy quieto + teclas de movimiento + speed2D por el piso
    //  (~0.0 al embestir una pared) + bloque solido delante a nivel de pies.
    if (!local.onGround() || vel.y > 0.06f)
    {
        stepping_ = false;
        return;
    }

    float speed2D = vecLength2D(vel);
    if (speed2D > blockedSpeed_)
    {
        stepping_ = false;
        return;
    }

    // Direccion INTENCIONADA (teclas + yaw): la velocidad real vale ~0
    // contra la pared; la intencion es lo que apunta al obstaculo.
    float fwd = axisOf('S', 'W');
    float str = axisOf('A', 'D');
    Vec3 dir = wishDirFromYaw(local.yaw(), fwd, str);
    if (vecLength2D(dir) < 0.01f)
    {
        stepping_ = false;
        return;
    }

    // Celda frontal a nivel de pies: sonda a dos distancias (0.8 cubre la
    // celda pegada, 1.35 la siguiente al ir apretado a la esquina).
    int fy = feetCellY;
    int fx = INT_MAX, fz = INT_MAX;
    bool found = false;
    const float probes[2] = {0.8f, 1.35f};
    for (float t : probes)
    {
        int px = (int)std::floor(exact.x + dir.x * t);
        int pz = (int)std::floor(exact.z + dir.z * t);
        int id = bs.getBlockId(px, fy, pz);
        if (isSolidId(id))
        {
            fx = px;
            fz = pz;
            found = true;
            break;
        }
    }
    if (!found)
    {
        stepping_ = false;
        return;
    }

    // Altura del obstaculo frontal (1..heightBlocks):
    // celdas solidas consecutivas desde el nivel de los pies.
    int h = heightBlocks_ < 1 ? 1 : (heightBlocks_ > 2 ? 2 : heightBlocks_);
    int oh = 0;
    for (int k = 1; k <= h; ++k)
    {
        int id = bs.getBlockId(fx, fy + k - 1, fz);
        if (!isSolidId(id))
            break;
        oh = k;
    }
    if (oh == 0)
    {
        stepping_ = false;
        return;
    }

    // Despeje requerido en la columna frontal y en la propia (4 celdas de
    // 0 explicito): sin aire CONFIRMADO (id==0), no se hace el step —
    // jamas se sube sobre un hueco desconocido.
    int front1 = bs.getBlockId(fx, fy + oh, fz);
    int front2 = bs.getBlockId(fx, fy + oh + 1, fz);
    int own1 = bs.getBlockId((int)std::floor(exact.x), fy + oh, (int)std::floor(exact.z));
    int own2 = bs.getBlockId((int)std::floor(exact.x), fy + oh + 1, (int)std::floor(exact.z));
    if (front1 != 0 || front2 != 0 || own1 != 0 || own2 != 0)
    {
        stepping_ = false;
        return;
    }

    if (mode_ == kMotion)
    {
        // Solo la componente Y: la fisica del juego resuelve el resto.
        Vec3 v = local.velocity();
        v.y = liftSpeed_;
        local.setVelocity(v);
        stepTargetY_ = (float)(fy + oh);
        stepping_ = true;

        char buf[80];
        snprintf(buf, sizeof(buf), "Motion step -> %.1f (h=%d)", stepTargetY_, oh);
        displayDebug(buf);
        return;
    }

    // --- Modo Clip: subida instantanea, patron Tower Clip del Scaffold ----
    // mPosition + AABB trasladado + move target (+0xD08), las 3 escrituras
    // juntas (el layout probado; escribir solo una desincroniza el render).
    ULONGLONG now = GetTickCount64();
    if (now - lastStepMs_ < 200ull)
        return;

    uintptr_t a = local.address();
    if (!a || !mem::isReadable(a, 0xD8))
        return;

    float dy = (float)(fy + oh) - feet.y;
    // Sanidad: nunca mover mas de 2 abajo/arriba ni valores degenerados.
    if (dy < 0.01f || dy > 2.01f)
        return;

    Vec3 np{exact.x, exact.y + dy, exact.z};

    float box[6] = {};
    if (!mem::rawRead(box, a + off::player::AabbMinX, sizeof(box)))
        return;
    float offMin[3] = {box[0] - exact.x, box[1] - exact.y, box[2] - exact.z};
    float offMax[3] = {box[3] - exact.x, box[4] - exact.y, box[5] - exact.z};

    float moved[6] = {
        np.x + offMin[0], np.y + offMin[1], np.z + offMin[2],
        np.x + offMax[0], np.y + offMax[1], np.z + offMax[2],
    };
    if (!mem::rawWrite(a + off::player::AabbMinX, moved, sizeof(moved)))
        return;

    mem::write<float>(a + off::player::PosTrueX, np.x);
    mem::write<float>(a + off::player::PosTrueY, np.y);
    mem::write<float>(a + off::player::PosTrueZ, np.z);
    mem::write<float>(a + off::player::MoveTargetX, np.x);
    mem::write<float>(a + off::player::MoveTargetY, np.y);
    mem::write<float>(a + off::player::MoveTargetZ, np.z);

    lastStepMs_ = now;

    char buf[80];
    snprintf(buf, sizeof(buf), "Clip step +%.2f (h=%d)", dy, oh);
    displayDebug(buf);
}

void Step::drawSettings()
{
    const char* modes[] = {"Motion", "Clip"};
    widgets::CCombo("Mode##step", &mode_, modes, 2);

    widgets::CSlider("Height##step", &heightBlocks_, 1, 2);

    if (mode_ == kMotion)
        widgets::CSlider("Lift speed##step", &liftSpeed_, 0.1f, 0.6f, "%.2f");

    widgets::CSlider("Blocked speed##step", &blockedSpeed_, 0.03f, 0.2f, "%.3f");
    widgets::Checkbox("Debug##step", &debug_);

    ImGui::TextDisabled("Motion: velocity lift, physics-resolved");
    ImGui::TextDisabled("Clip: instant teleport (move target)");
}

nlohmann::json Step::saveSettings() const
{
    return nlohmann::json{
        {"mode", mode_},
        {"height", heightBlocks_},
        {"liftSpeed", liftSpeed_},
        {"blockedSpeed", blockedSpeed_},
        {"debug", debug_},
    };
}

void Step::loadSettings(const nlohmann::json& j)
{
    mode_ = j.value("mode", mode_);
    if (mode_ != kMotion && mode_ != kClip)
        mode_ = kMotion;

    heightBlocks_ = j.value("height", heightBlocks_);
    if (heightBlocks_ < 1)
        heightBlocks_ = 1;
    if (heightBlocks_ > 2)
        heightBlocks_ = 2;

    liftSpeed_ = clampf(j.value("liftSpeed", liftSpeed_), 0.1f, 0.6f);
    blockedSpeed_ = clampf(j.value("blockedSpeed", blockedSpeed_), 0.03f, 0.2f);
    debug_ = j.value("debug", debug_);
}

MC_REGISTER_MODULE(Step);

}
