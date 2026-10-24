#include "Scaffold.hpp"

#include "Features/Rotation.hpp"
#include "Features/RotationHelper.hpp"
#include "Features/WorldChunks.hpp"
#include "Framework/Math.hpp"
#include "Framework/Memory.hpp"
#include "Framework/Log.hpp"
#include "GUI/Widgets.hpp"
#include "SDK/ChatHook.hpp"
#include "SDK/Classes/GameMode.hpp"
#include "SDK/Classes/Inventory.hpp"
#include "SDK/Classes/ItemInstance.hpp"
#include "SDK/Classes/LocalPlayer.hpp"
#include "SDK/Game.hpp"

#include <windows.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <climits>
#include <vector>

namespace mc {

namespace {

bool keyDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

float axisOf(int negVk, int posVk)
{
    float v = 0.0f;
    if (keyDown(posVk))
        v += 1.0f;
    if (keyDown(negVk))
        v -= 1.0f;
    return v;
}

// [SCDBG]: razon exacta de cada abort (1 log/seg max).
void scDbg(const char* fmt, ...)
{
    static ULONGLONG s_last = 0;
    ULONGLONG now = GetTickCount64();
    if (now - s_last < 1000ull)
        return;
    s_last = now;
    char buf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    MC_LOG("[SCDBG] %s", buf);
}

// Celda donde puede ir un bloque NUEVO: aire o liquido reemplazable.
bool isPlaceableCell(const BlockSource& bs, int x, int y, int z)
{
    if (y < -64 || y > 400)
        return false;
    int id = bs.getBlockId(x, y, z);
    return id <= 0 || id == 8 || id == 10;
}

// Bloque que sirve de SOPORTE (solido, ni agua ni lava).
bool isSupport(const BlockSource& bs, int x, int y, int z)
{
    if (y < -64 || y > 400)
        return false;
    int id = bs.getBlockId(x, y, z);
    return id > 0 && id != 8 && id != 10;
}

// Puerto de BlockUtils::getBlockPlaceFace (Solstice): la cara del vecino
// solidÐ¾ contra la que se coloca. Orden de preferencia: abajo (top face,
// la natural del puente), luego los 4 laterales, arriba al final (esa es
// la que mAvoidUnderplace evita).
int getBlockPlaceFace(const BlockSource& bs, int x, int y, int z)
{
    static const struct
    {
        int face;
        int dx, dy, dz;
    } kFaces[6] = {
        {1, 0, -1, 0}, // soporte abajo -> cara top del soporte
        {2, 0, 0, 1},  // soporte +z   -> cara north (z-min)
        {3, 0, 0, -1}, // soporte -z   -> cara south (z-max)
        {4, 1, 0, 0},  // soporte +x   -> cara west (x-min)
        {5, -1, 0, 0}, // soporte -x   -> cara east (x-max)
        {0, 0, 1, 0},  // soporte arriba -> cara bottom
    };
    for (const auto& f : kFaces)
    {
        if (isSupport(bs, x + f.dx, y + f.dy, z + f.dz))
            return f.face;
    }
    return -1;
}

// Puerto de BlockUtils::getClosestPlacePos (Solstice): espiral por anillos
// alrededor de la celda pedida â€” la celda valida (colocable + con cara)
// mas cercana dentro del rango.
bool getClosestPlacePos(const BlockSource& bs, int cx, int cy,
                         int cz, int range, int& ox, int& oy, int& oz)
{
    for (int r = 1; r <= range; ++r)
    {
        int bestD2 = INT_MAX;
        bool found = false;
        for (int dx = -r; dx <= r; ++dx)
        {
            for (int dy = -r; dy <= r; ++dy)
            {
                for (int dz = -r; dz <= r; ++dz)
                {
                    int ad = std::max(std::abs(dx), std::max(std::abs(dy), std::abs(dz)));
                    if (ad != r)
                        continue; // solo el borde del cubo (el anillo)
                    int x = cx + dx, y = cy + dy, z = cz + dz;
                    if (y < -64 || y > 400)
                        continue;
                    if (!isPlaceableCell(bs, x, y, z))
                        continue;
                    if (getBlockPlaceFace(bs, x, y, z) == -1)
                        continue;
                    int d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 < bestD2)
                    {
                        bestD2 = d2;
                        ox = x;
                        oy = y;
                        oz = z;
                        found = true;
                    }
                }
            }
        }
        if (found)
            return true; // el primer anillo con resultado es el mas cercano
    }
    return false;
}

// Puerto de Scaffold::getRotBasedPos (Solstice): celda a `yPos` a `extend`
// bloques en la direccion de VISTA del player (yaw + 90 -> cos/sin).
void getRotBasedPos(const LocalPlayer& lp, float extend, float yPos, int& ox, int& oy, int& oz)
{
    float yaw = deg2rad(lp.yaw() + 90.0f);
    Vec3 p = lp.posExact();
    ox = (int)std::floor(p.x + std::cos(yaw) * extend);
    oy = (int)std::floor(yPos);
    oz = (int)std::floor(p.z + std::sin(yaw) * extend);
}

// Yaw "Reverse" (la rotacion del Telly): mira de ESPALDAS a la direccion
// de movimiento real (WASD + camara), adaptada a diagonales/strafe.
float reverseYaw(const LocalPlayer& lp, float fwd, float strafe)
{
    Vec3 wish = wishDirFromYaw(lp.yaw(), fwd, strafe);
    if (vecLength2D(wish) > 0.01f)
        return wrapDegrees(rad2deg(std::atan2(-wish.x, wish.z)) + 180.0f);
    return wrapDegrees(lp.yaw() + 180.0f);
}

// Tower Velocity: escribir solo la componente Y de la velocidad.
void setVelocityY(const LocalPlayer& lp, float vy)
{
    mem::write<float>(lp.address() + off::player::VelocityY, vy);
}

// Tower Clip (Solstice): player->setPosition(pos + (0, dy, 0)) â€” escribir
// mPosition y trasladar el AABB intacto (el patron probado del Backtrack,
// pero sin el check de vtable remota).
void clipLocalPlayerUp(const LocalPlayer& lp, float dy)
{
    uintptr_t a = lp.address();
    if (!a || !mem::isReadable(a, 0xD8))
        return;

    Vec3 real = lp.posExact();
    Vec3 np{real.x, real.y + dy, real.z};

    float box[6] = {};
    if (!mem::rawRead(box, a + off::player::AabbMinX, sizeof(box)))
        return;
    float offMin[3] = {box[0] - real.x, box[1] - real.y, box[2] - real.z};
    float offMax[3] = {box[3] - real.x, box[4] - real.y, box[5] - real.z};

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
}

} // namespace

Scaffold::Scaffold()
    : Module("Scaffold", "Automatically places blocks beneath you while moving",
             Category::Movement, 0)
{
    markHasSettings();
}

void Scaffold::onEnable()
{
    rotation_helper::clearClientRotation(rot::PrioMovement);
    rot::clear(rot::PrioMovement);
    lastSwapMs_ = 0;
    mStartY_ = 0.0f;
    mIsTowering_ = false;
    mLastSlot_ = -1;
    lastRewindMs_ = 0;
    lastStrafe_ = 1;

    Game& game = Game::get();
    if (game.localFound())
    {
        LocalPlayer lp = game.localPlayer();
        if (lp.valid())
        {
            // Solstice onEnable: mStartY = pos.y - 2.62 (el bloque bajo los
            // pies) y recordar el slot para restaurarlo al desactivar.
            mStartY_ = lp.posExact().y - 2.62f;
            mLastSlot_ = lp.hotbarSlot();
        }
    }
}

void Scaffold::onDisable()
{
    rotation_helper::clearClientRotation(rot::PrioMovement);
    rot::clear(rot::PrioMovement);
    Game& game = Game::get();
    if (game.localFound())
    {
        LocalPlayer lp = game.localPlayer();
        if (lp.valid())
        {
            if (mLastSlot_ >= 0 && mLastSlot_ <= 8 && mLastSlot_ != lp.hotbarSlot())
                lp.setHotbarSlot(mLastSlot_);
            if (mIsTowering_)
            {
                mIsTowering_ = false;
                setVelocityY(lp, -5.0f);
            }
        }
    }
}

// Puerto de Scaffold::tickPlace (Solstice).
bool Scaffold::tickPlace(const LocalPlayer& lp,
                         const BlockSource& bs)
{
    LocalPlayer& mlp = const_cast<LocalPlayer&>(lp);
    Vec3 vel = mlp.velocity();
    Vec3 exact = mlp.posExact();

    // --- mStartY: el Y del puente (lock hacia abajo, como Solstice) ---
    float currentY = exact.y - 2.62f;
    if (!lockY_)
        mStartY_ = currentY;
    if (currentY < mStartY_)
        mStartY_ = currentY;

    bool isMoving = keyDown('W') || keyDown('A') || keyDown('S') || keyDown('D');
    bool space = keyDown(VK_SPACE);

    // RewindSide: strafe sticky (A=-1, D=+1; ambos o ninguno = ultimo).
    float strafe = axisOf('A', 'D');
    if (strafe > 0.0f)
        lastStrafe_ = 1;
    else if (strafe < 0.0f)
        lastStrafe_ = -1;

    // Space sin moverse desbloquea el Y (tower vanilla sube el piso).
    if (space && !isMoving)
        mStartY_ = currentY;

    float maxExtend = isMoving ? extend_ : 0.0f;

    // --- Tower (Solstice Velocity/Clip) — solo en modo Normal: en Telly el
    // espacio es el salto del puente y en Rewind es ground-bridging puro.
    if (mode_ == kNormal && towerMode_ != 0 && space &&
        (allowMovement_ || !isMoving) &&
        (mlp.onGround() || vel.y > -0.5f))
    {
        if (!allowMovement_ && towerMode_ == 1)
        {
            mem::write<float>(mlp.address() + off::player::VelocityX, 0.0f);
            mem::write<float>(mlp.address() + off::player::VelocityZ, 0.0f);
        }
        mStartY_ = exact.y;
        mIsTowering_ = true;
        if (towerMode_ == 1)
            setVelocityY(lp, towerSpeed_ / 10.0f);
        else
            clipLocalPlayerUp(lp, 1.0f * (towerSpeed_ / 10.0f));
        maxExtend = 0.0f;
    }
    else if (mIsTowering_)
    {
        mIsTowering_ = false;
        setVelocityY(lp, -5.0f);
    }

    // --- Item: slot activo si tiene algo colocable; sino swap al mejor.
    // Patron del Python v6 (probado): el gate fuerte es el Item* no-null
    // — el ORQUESTADOR del juego valida el item de verdad. El
    // isBlockItem por vtable es PREFERENCIA: cuando al menos un slot la
    // pasa, se usan solo esos; cuando NINGUN la pasa (lista de vtables
    // incompleta — blockItems=0 con slots llenos), fallback a cualquier
    // slot con Item* y el orquestador decide.
    ItemInstance heldNow = mlp.heldItem();
    bool heldOk = heldNow.valid() && heldNow.item() != 0;
    if (!heldOk)
    {
        Inventory inv = mlp.inventory();
        int best = -1;
        int bestCount = 0;
        int withItem = 0, blockItems = 0, withItemPtr = 0;
        uintptr_t firstItemVt = 0;
        for (int slot = 0; slot <= 8; ++slot)
        {
            uintptr_t addr = inv.hotbarItem(slot);
            if (!addr)
                continue;
            ++withItem;
            ItemInstance it(addr);
            if (!it.valid() || it.item() == 0)
                continue;
            ++withItemPtr;
            if (firstItemVt == 0)
                firstItemVt = it.itemVtable();
            if (!it.isBlockItem())
                continue;
            ++blockItems;
            // Block items verificados SIEMPRE ganan (prioridad maxima).
            int c = (it.count() > 0 ? it.count() : 1) + 1000000;
            if (best == -1 || c > bestCount)
            {
                best = slot;
                bestCount = c;
            }
        }
        if (best < 0 && withItemPtr > 0)
        {
            // Fallback v6: ningun block item verificado pero hay items —
            // elegir el de mayor count aparente; el orquestador decidira
            // si es colocable (el failStreak rota si falla).
            scDbg("0 block items en %d slots con item "
                  "(vt=0x%llX RVA 0x%llX) — fallback v6 activo",
                  withItem, (unsigned long long)firstItemVt,
                  (unsigned long long)(firstItemVt - mem::base()));
            for (int slot = 0; slot <= 8; ++slot)
            {
                uintptr_t addr = inv.hotbarItem(slot);
                if (!addr)
                    continue;
                ItemInstance it(addr);
                if (!it.valid() || it.item() == 0)
                    continue;
                int c = it.count() > 0 ? it.count() : 1;
                if (best == -1 || c > bestCount)
                {
                    best = slot;
                    bestCount = c;
                }
            }
        }
        if (best < 0)
        {
            scDbg("sin items: slotsConItem=%d conItemPtr=%d "
                  "held(addr=0x%llX valid=%d)",
                  withItem, withItemPtr,
                  (unsigned long long)heldNow.address(),
                  heldNow.valid() ? 1 : 0);
            return false;
        }
        unsigned long long now = GetTickCount64();
        if (now - lastSwapMs_ < (unsigned long long)switchDelayMs_)
            return false;
        // El slot que lee el orquestador para el flag del paquete es
        // supplies+0xB0 (inv SelectedSlot) â€” escribir ambos.
        inv.setSelectedSlot(best);
        mlp.setHotbarSlot(best);
        lastSwapMs_ = now;
        scDbg("swap de slot -> %d", best);
        return false; // el proximo tick coloca ya con el slot cambiado
    }

    ItemInstance held = mlp.heldItem();

    // --- getPlacePos(0): celda base segun el modo ---
    int bx, by, bz;
    // Ventana Telly: todo el vuelo (aunque sueltes space tras el salto) +
    // el instante del despegue con space. Si solo se miraba space, al
    // soltarlo en el aire la rotacion dejaba de adaptarse.
    bool tellyAir = (mode_ == kTelly) && isMoving && (!mlp.onGround() || space);

    if (tellyAir)
    {
        // Telly: celda bajo el jugador (con lead XZ segun velocidad mientras
        // estas en el aire) a mStartY_ — el bloque sobre el que caeras.
        float px = exact.x;
        float pz = exact.z;
        if (!mlp.onGround())
        {
            px += vel.x * tellyLead_;
            pz += vel.z * tellyLead_;
        }
        bx = (int)std::floor(px);
        by = (int)std::floor(mStartY_);
        bz = (int)std::floor(pz);
        maxExtend = 0.0f;

        // Rotacion Telly: se PIDE AQUI (al seleccionar target), no tras el
        // place — el giro debe existir durante toda la ventana aerea aunque
        // el place falle/reintente. Usa el yaw "Reverse" (direccion de
        // movimiento + 180) y pitch casi vertical. 400ms: se renueva cada
        // tick mientras vuelas y no queda pegada mucho despues de aterrizar.
        const float clientYaw = reverseYaw(mlp, axisOf('S', 'W'), strafe);
        rotation_helper::requestClientRotation(mlp, clientYaw, 89.9f, rot::PrioMovement);
        rot::request(89.9f, clientYaw, 400, rot::PrioMovement);
    }
    else if (mode_ == kRewind)
    {
        // RewindSide: side-bridge sin girar la vista — la celda va 1 bloque
        // en la DIRECCION del strafe activo (donde te desplazas), con la
        // cadencia "rewind" y rotacion honesta hacia la cara.
        float yawRad = deg2rad(mlp.yaw());
        float rx = -std::cos(yawRad); // right.x
        float rz = -std::sin(yawRad); // right.z
        float side = (float)lastStrafe_;
        bx = (int)std::floor(exact.x + rx * side);
        by = (int)std::floor(mStartY_);
        bz = (int)std::floor(exact.z + rz * side);
        maxExtend = 0.0f;
    }
    else
    {
        getRotBasedPos(mlp, 0.0f, mStartY_, bx, by, bz);
    }

    // Al caer mas de ~3 bloques, buscar mas abajo (Solstice: pos.y-3.62).
    if (!mlp.onGround() && vel.y < -0.5f && !mIsTowering_)
        by = (int)std::floor(exact.y) - 2;

    if (getBlockPlaceFace(bs, bx, by, bz) == -1)
    {
        int rx, ry, rz;
        if (!getClosestPlacePos(bs, bx, by, bz, (int)range_, rx, ry, rz))
        {
            scDbg("sin celda colocable cerca de (%d,%d,%d) rango=%d",
                  bx, by, bz, (int)range_);
            return false;
        }
        bx = rx;
        by = ry;
        bz = rz;
    }

    // --- Extend loop (Solstice): si la base no es aire, avanzar. ---
    if (!isPlaceableCell(bs, bx, by, bz) && !mIsTowering_)
    {
        for (float e = 1.0f; e <= maxExtend; e += 1.0f)
        {
            int ex, ey, ez;
            getRotBasedPos(mlp, e, mStartY_, ex, ey, ez);
            if (isPlaceableCell(bs, ex, ey, ez))
            {
                bx = ex;
                by = ey;
                bz = ez;
                break;
            }
        }
    }

    if (!isPlaceableCell(bs, bx, by, bz))
    {
        scDbg("target (%d,%d,%d) no es aire", bx, by, bz);
        return false;
    }

    int face = getBlockPlaceFace(bs, bx, by, bz);
    if (face < 0)
    {
        scDbg("target (%d,%d,%d) sin cara colocable", bx, by, bz);
        return false;
    }
    if (avoidUnderplace_ && face == 0)
    {
        scDbg("avoidUnderplace: face 0 (soporte arriba)");
        return false;
    }

    // RewindSide: cadencia de place — el ritmo acompana el strafe (una
    // colocacion cada rewindDelay ms), evita el spam por tick del modo
    // Normal mientras alterna lados.
    if (mode_ == kRewind && rewindDelayMs_ > 0)
    {
        ULONGLONG nowRw = GetTickCount64();
        if (nowRw - lastRewindMs_ < (unsigned long long)rewindDelayMs_)
            return false;
    }

    // --- Soporte + clickPos: centro de la cara con 0.99/0.01 en bordes ---
    int sx = bx, sy = by, sz = bz;
    float cx, cy, cz;
    switch (face)
    {
    case 0: // soporte arriba -> cara bottom
        sy = by + 1;
        cx = (float)sx + 0.5f; cy = (float)sy + 0.01f; cz = (float)sz + 0.5f;
        break;
    case 1: // soporte abajo -> cara top
        sy = by - 1;
        cx = (float)sx + 0.5f; cy = (float)sy + 0.99f; cz = (float)sz + 0.5f;
        break;
    case 2: // soporte +z -> cara north (z-min)
        sz = bz + 1;
        cx = (float)sx + 0.5f; cy = (float)sy + 0.5f; cz = (float)sz + 0.01f;
        break;
    case 3: // soporte -z -> cara south (z-max)
        sz = bz - 1;
        cx = (float)sx + 0.5f; cy = (float)sy + 0.5f; cz = (float)sz + 0.99f;
        break;
    case 4: // soporte +x -> cara west (x-min)
        sx = bx + 1;
        cx = (float)sx + 0.01f; cy = (float)sy + 0.5f; cz = (float)sz + 0.5f;
        break;
    case 5: // soporte -x -> cara east (x-max)
        sx = bx - 1;
        cx = (float)sx + 0.99f; cy = (float)sy + 0.5f; cz = (float)sz + 0.5f;
        break;
    default:
        return false;
    }

    // --- placeBlock (Solstice): el orquestador real del juego ---
    BlockPos support{sx, sy, sz};
    Vec3 clickAbs{cx, cy, cz};
    bool ok = placeBlockFull(held, support, (uint8_t)face, clickAbs, mlp.address());

    if (!ok)
    {
        // El orquestador valido y rechazo (colision con tu propio
        // hitbox, cooldown, item en estado raro). Como el v6/Solstice:
        // NADA de fallback de paquete manual — mandar un place que el
        // propio cliente acaba de rechazar crea bloques fantasma y
        // desync con el server. El proximo tick reintenta con la
        // geometria nueva; el failStreak hace el swap si el held esta
        // agotado.
        if (++failStreak_ >= 2)
        {
            Inventory inv = mlp.inventory();
            int cur = mlp.hotbarSlot();
            int best = -1;
            int bestCount = 0;
            for (int slot = 0; slot <= 8; ++slot)
            {
                if (slot == cur)
                    continue;
                uintptr_t addr = inv.hotbarItem(slot);
                if (!addr)
                    continue;
                ItemInstance it(addr);
                if (!it.valid() || it.item() == 0)
                    continue;
                int c = it.count() > 0 ? it.count() : 1;
                if (it.isBlockItem())
                    c += 1000000;
                if (best == -1 || c > bestCount)
                {
                    best = slot;
                    bestCount = c;
                }
            }
            if (best >= 0)
            {
                inv.setSelectedSlot(best);
                mlp.setHotbarSlot(best);
                scDbg("swap forzado tras fallos -> slot %d (antes %d)", best, cur);
            }
            failStreak_ = 0;
        }
        return false;
    }
    failStreak_ = 0;

    static ULONGLONG s_lastOk = 0;
    ULONGLONG now = GetTickCount64();
    lastPlaceMs_ = now;
    if (mode_ == kRewind)
        lastRewindMs_ = now;

    // (ok == true garantizado aqui: el if (!ok) de arriba ya retorno)

    // --- Silent rotation (Solstice onPacketOutEvent, adaptado) ---
    // Setear la rotacion hacia la cara del bloque recien colocado;
    // el hook del MovePlayerPacket la aplica al paquete saliente.
    // En Telly se re-ancla SIEMPRE a la cara real (el request temprano
    // solo cubria la ventana aerea mientras el place reintenta).
    if ((!tellyAir && rotateMode_ > 0) || (mode_ == kTelly && tellyAir))
    {
        int ox = 0, oy = 0, oz = 0;
        placeFaceOffset(face, ox, oy, oz);
        Vec3 target{
            (float)bx + (float)ox * 0.5f,
            (float)by + (float)oy * 0.5f,
            (float)bz + (float)oz * 0.5f,
        };

        // MathUtils::getRots (Solstice) — desde la posicion de OJOS
        // del actor (getPos() en Solstice), no desde los pies: es la
        // misma referencia que usa el server para validar.
        Vec3 eye = mlp.eyePos();
        float dx = target.x - eye.x;
        float dy = target.y - eye.y;
        float dz = target.z - eye.z;
        float dist = std::sqrt(dx * dx + dz * dz);
        float calcYaw = rad2deg(std::atan2(dz, dx)) - 90.0f;
        float calcPitch = rad2deg(-std::atan2(dy, dist));

        // Normalizar el yaw calculado a (-180, 180] — Solstice lo
        // hace en todos los modos antes de aplicar.
        auto wrapYaw = [](float y) {
            while (y > 180.0f)
                y -= 360.0f;
            while (y < -180.0f)
                y += 360.0f;
            return y;
        };
        calcYaw = wrapYaw(calcYaw);

        float outYaw = calcYaw;
        float outPitch = calcPitch;

        // Reverse adaptativo: si el bloque tiene desplazamiento horizontal
        // suficiente, se mira de espaldas a SU CARA (calcYaw + 180, como
        // Backwards); si cae casi debajo de ti (Telly), calcYaw degenera y
        // se usa la direccion de movimiento + 180 — siempre "de espaldas".
        auto adaptiveReverseYaw = [&](float dxT, float dzT) {
            float horiz = std::sqrt(dxT * dxT + dzT * dzT);
            if (horiz >= 0.35f)
                return wrapYaw(calcYaw + 180.0f);
            return reverseYaw(mlp, axisOf('S', 'W'), strafe);
        };

        if (mode_ == kTelly && tellyAir)
        {
            // Telly: Reverse anclado a la cara del bloque colocado.
            outYaw = adaptiveReverseYaw(dx, dz);
            outPitch = 89.9f;
        }
        else
        {
            switch (rotateMode_)
            {
            case 1: // Normal (Solstice): yaw propio normalizado, pitch >= 82
                outYaw = wrapYaw(mlp.yaw());
                if (outPitch < 82.0f)
                    outPitch = 82.0f;
                break;
            case 2: // Down (Solstice): pitch 89.9 — el yaw QUEDA el
                    // calculado hacia el bloque (no se toca, como Solstice)
                outYaw = calcYaw;
                outPitch = 89.9f;
                break;
            case 3: // Backwards (Solstice): yaw del bloque + 180; el
                    // pitch queda el calculado (sin tocar)
                outYaw = wrapYaw(calcYaw + 180.0f);
                outPitch = calcPitch;
                break;
            case 4: // Reverse (la rotacion del Telly): espaldas a la CARA
                    // colocada con pitch casi vertical (89.9); si el bloque
                    // esta debajo, espaldas a la direccion de movimiento.
                outYaw = adaptiveReverseYaw(dx, dz);
                outPitch = 89.9f;
                break;
            }
        }

        // Apply the same target to outgoing movement packets and the local
        // rendered model, while leaving the camera untouched.
        rotation_helper::requestClientRotation(mlp, outYaw, outPitch, rot::PrioMovement);
        rot::request(outPitch, outYaw, 500, rot::PrioMovement);
    }

    if (now - s_lastOk > 1000ull)
    {
        s_lastOk = now;
        MC_LOG("[SC] place support=(%d,%d,%d) face=%d ok=1", sx, sy, sz, face);
    }
    return true;
}

void Scaffold::onTick()
{
    // La request de rotacion expira sola (rot::current chequea el
    // expiry); no hace falta limpieza manual — el Present deja de
    // escribir el actor y el paquete sale con la rotacion real.

    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer lp = game.localPlayer();
    if (!lp.valid())
        return;

    BlockSource bs = game.blockSource();
    if (!bs.valid())
        return;

    // Solstice onBaseTickEvent: `places` intentos por tick. El lector de
    // bloques es el BlockSource::getBlock real del juego — sin cache de
    // chunks (en server los chunks stale causaban los "deja de funcionar
    // un rato").
    int places = std::max(1, std::min(20, (int)places_));
    for (int i = 0; i < places; ++i)
    {
        if (!tickPlace(lp, bs))
            break;
    }
}

nlohmann::json Scaffold::saveSettings() const
{
    return nlohmann::json{
        {"mode", mode_},
        {"places", places_},
        {"extend", extend_},
        {"range", range_},
        {"lockY", lockY_},
        {"avoidUnderplace", avoidUnderplace_},
        {"towerMode", towerMode_},
        {"towerSpeed", towerSpeed_},
        {"allowMovement", allowMovement_},
        {"switchDelayMs", switchDelayMs_},
        {"rotateMode", rotateMode_},
        {"tellyLead", tellyLead_},
        {"rewindDelayMs", rewindDelayMs_},
    };
}

void Scaffold::loadSettings(const nlohmann::json& j)
{
    mode_ = j.value("mode", mode_);
    if (mode_ < kNormal || mode_ > kRewind)
        mode_ = kNormal;
    places_ = j.value("places", places_);
    if (places_ < 1.0f)
        places_ = 1.0f;
    if (places_ > 20.0f)
        places_ = 20.0f;
    extend_ = j.value("extend", extend_);
    if (extend_ < 0.0f)
        extend_ = 0.0f;
    if (extend_ > 10.0f)
        extend_ = 10.0f;
    range_ = j.value("range", range_);
    if (range_ < 0.0f)
        range_ = 0.0f;
    if (range_ > 10.0f)
        range_ = 10.0f;
    lockY_ = j.value("lockY", lockY_);
    avoidUnderplace_ = j.value("avoidUnderplace", avoidUnderplace_);
    towerMode_ = j.value("towerMode", towerMode_);
    if (towerMode_ < 0)
        towerMode_ = 0;
    if (towerMode_ > 2)
        towerMode_ = 2;
    towerSpeed_ = j.value("towerSpeed", towerSpeed_);
    if (towerSpeed_ < 0.0f)
        towerSpeed_ = 0.0f;
    if (towerSpeed_ > 20.0f)
        towerSpeed_ = 20.0f;
    allowMovement_ = j.value("allowMovement", allowMovement_);
    switchDelayMs_ = j.value("switchDelayMs", switchDelayMs_);
    rotateMode_ = j.value("rotateMode", rotateMode_);
    if (rotateMode_ < 0)
        rotateMode_ = 0;
    if (rotateMode_ > 4)
        rotateMode_ = 4;

    tellyLead_ = clampf(j.value("tellyLead", tellyLead_), 0.0f, 0.6f);
    rewindDelayMs_ = j.value("rewindDelayMs", rewindDelayMs_);
    if (rewindDelayMs_ < 0)
        rewindDelayMs_ = 0;
    if (rewindDelayMs_ > 250)
        rewindDelayMs_ = 250;
}

void Scaffold::drawSettings()
{
    const char* modes[] = {"Normal", "Telly", "RewindSide"};
    widgets::CCombo("Mode##sc", &mode_, modes, 3);

    if (mode_ == kTelly)
    {
        ImGui::Indent();
        widgets::CSlider("Telly lead##sc", &tellyLead_, 0.0f, 0.6f, "%.2f s");
        ImGui::TextDisabled("Jump + move + space: places under you mid-air");
        ImGui::TextDisabled("with a silent 180-degree look (pitch 89.9)");
        ImGui::Unindent();
    }
    else if (mode_ == kRewind)
    {
        ImGui::Indent();
        widgets::CSlider("Rewind delay##sc", &rewindDelayMs_, 0, 250, "%d ms");
        ImGui::TextDisabled("Side-bridge: places one block toward your strafe");
        ImGui::TextDisabled("Look rotation follows the Rotate mode setting");
        ImGui::Unindent();
    }

    ImGui::Separator();
    widgets::CSlider("Places##sc", &places_, 1.0f, 20.0f, "%.0f");
    widgets::CSlider("Extend##sc", &extend_, 0.0f, 10.0f, "%.0f");
    widgets::CSlider("Range##sc", &range_, 0.0f, 10.0f, "%.0f");
    widgets::Checkbox("Lock Y##sc", &lockY_);
    widgets::Checkbox("Avoid underplace##sc", &avoidUnderplace_);
    ImGui::Separator();
    const char* towers[] = {"Vanilla", "Velocity", "Clip"};
    widgets::CCombo("Tower mode##sc", &towerMode_, towers, 3);
    widgets::CSlider("Tower speed##sc", &towerSpeed_, 0.0f, 20.0f, "%.1f");
    widgets::Checkbox("Allow movement (tower)##sc", &allowMovement_);
    widgets::CSlider("Switch delay (ms)##sc", &switchDelayMs_, 0, 500, "%d");
    ImGui::Separator();
    const char* rots[] = {"None", "Normal", "Down", "Backwards", "Reverse"};
    widgets::CCombo("Rotate mode##sc", &rotateMode_, rots, 5);
    ImGui::TextDisabled("Silent rotations apply to movement packets and the local model");
    ImGui::TextDisabled("Smooth body yaw and head yaw/pitch; camera stays unchanged");
    ImGui::TextDisabled("Reverse = back to the placed face, pitch 89.9 (Telly look)");
}

MC_REGISTER_MODULE(Scaffold);

}
