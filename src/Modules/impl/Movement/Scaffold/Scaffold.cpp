#include "Scaffold.hpp"

#include "Features/Rotation.hpp"
#include "Features/RotationHelper.hpp"
#include "SDK/World/LevelChunkCache.hpp"
#include "Framework/Math.hpp"
#include "Framework/Memory.hpp"
#include "Framework/Log.hpp"
#include "GUI/Widgets.hpp"
#include "SDK/Gameplay/GameMode.hpp"
#include "SDK/Inventory/Inventory.hpp"
#include "SDK/Inventory/ItemInstance/ItemInstance.hpp"
#include "SDK/Entity/LocalPlayer.hpp"
#include "SDK/Network/Packets/MobEquipmentPacket.hpp"
#include "SDK/Core/Game.hpp"
#include "SDK/UI/Hooks/ChatHook.hpp"

#include <windows.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <climits>
#include <exception>
#include <vector>

namespace mc {

namespace {

bool keyDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool hasReadableItem(const ItemInstance& stack)
{
    if (!stack.valid())
        return false;
    const uintptr_t item = stack.item();
    return item > 0x10000 && item != UINTPTR_MAX &&
           mem::isReadable(item, sizeof(uintptr_t));
}

int safeStackCount(const ItemInstance& stack)
{
    // The count field at +0x18 is not fully verified for every stack variant
    // in this build. Reject impossible values instead of letting them dominate
    // slot selection or appear as an enormous HUD total.
    const int count = stack.count();
    return count >= 1 && count <= 64 ? count : 1;
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

// Solstice aplica el offset de las teclas al yaw al calcular Extend. Esto
// hace que el objetivo avance en la direccion de movimiento al strafear o
// retroceder, sin cambiar la celda base (extend == 0).
float movementKeyYawOffset()
{
    const bool w = keyDown('W');
    const bool a = keyDown('A');
    const bool s = keyDown('S');
    const bool d = keyDown('D');

    if (w && a) return -45.0f;
    if (w && d) return 45.0f;
    if (s && a) return -135.0f;
    if (s && d) return 135.0f;
    if (w) return 0.0f;
    if (a) return -90.0f;
    if (s) return -180.0f;
    if (d) return 90.0f;
    return 0.0f;
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
    // -1 means the block lookup is unknown (for example, an unloaded server
    // chunk), not air. Treating it as air made Scaffold send a placement the
    // server could not accept once it reached an edge or chunk boundary.
    return id == 0 || (id >= 8 && id <= 11);
}

// Bloque que sirve de SOPORTE (solido, ni agua ni lava).
bool isSupport(const BlockSource& bs, int x, int y, int z)
{
    if (y < -64 || y > 400)
        return false;
    int id = bs.getBlockId(x, y, z);
    return id > 0 && !(id >= 8 && id <= 11);
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
    float yaw = deg2rad(lp.yaw() + movementKeyYawOffset() + 90.0f);
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

// Keep transient access violations local to Scaffold. The global module
// guard disables a module after an uncaught SEH exception; this boundary lets
// the next game tick reconcile its slot state and retry instead.
bool sehScaffoldTick(Scaffold* scaffold)
{
    if (!scaffold)
        return false;
    __try
    {
        scaffold->onTickImpl();
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        scaffold->recoverAfterTickFault(GetExceptionCode());
        return false;
    }
}

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
    mStartY_ = 0.0f;
    mIsTowering_ = false;
    mLastSlot_ = -1;
    mLastInventorySlot_ = -1;
    mServerSlot_ = -1;
    forcedBlockSlot_ = -1;
    failStreak_ = 0;
    lastRewindMs_ = 0;
    lastStrafe_ = 1;
    slotRecoveryPending_ = false;
    recoveryActorSlot_ = -1;
    recoveryInventorySlot_ = -1;
    recoveryServerSlot_ = -1;
    lastRecoveryAttemptMs_ = 0;
    lastExceptionLogMs_ = 0;
    nextRetryMs_ = 0;

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
            Inventory inventory = lp.inventory();
            if (inventory.valid())
                mLastInventorySlot_ = inventory.selectedSlot();
            mServerSlot_ = mLastSlot_;
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
            Inventory inventory = lp.inventory();
            if (slotRecoveryPending_)
                (void)recoverSlots(lp);
            if (mLastSlot_ >= 0 && mLastSlot_ <= 8 && mLastSlot_ != lp.hotbarSlot())
                lp.setHotbarSlot(mLastSlot_);
            if (inventory.valid() && mLastInventorySlot_ >= 0 &&
                mLastInventorySlot_ <= 8 && inventory.selectedSlot() != mLastInventorySlot_)
                inventory.setSelectedSlot(mLastInventorySlot_);
            if (mServerSlot_ >= 0 && mLastSlot_ >= 0 && mServerSlot_ != mLastSlot_)
                (void)packets::sendMobEquipment(lp, mLastSlot_);
            if (mIsTowering_)
            {
                mIsTowering_ = false;
                setVelocityY(lp, -5.0f);
            }
        }
    }
}

void Scaffold::beginSlotRecovery(int actorSlot, int inventorySlot, int serverSlot)
{
    recoveryActorSlot_ = actorSlot;
    recoveryInventorySlot_ = inventorySlot;
    recoveryServerSlot_ = serverSlot >= 0 && serverSlot <= 8 ? serverSlot : actorSlot;
    slotRecoveryPending_ = true;
}

void Scaffold::finishSlotRecovery()
{
    slotRecoveryPending_ = false;
    recoveryActorSlot_ = -1;
    recoveryInventorySlot_ = -1;
    recoveryServerSlot_ = -1;
}

bool Scaffold::recoverSlots(const LocalPlayer& player)
{
    if (!slotRecoveryPending_ || !player.valid())
        return !slotRecoveryPending_;

    Inventory inventory = player.inventory();
    if (recoveryActorSlot_ >= 0 && recoveryActorSlot_ <= 8 &&
        player.hotbarSlot() != recoveryActorSlot_)
        const_cast<LocalPlayer&>(player).setHotbarSlot(recoveryActorSlot_);
    if (inventory.valid() && recoveryInventorySlot_ >= 0 && recoveryInventorySlot_ <= 8 &&
        inventory.selectedSlot() != recoveryInventorySlot_)
        inventory.setSelectedSlot(recoveryInventorySlot_);

    const unsigned long long now = GetTickCount64();
    if (now - lastRecoveryAttemptMs_ < 100ull)
        return false;
    lastRecoveryAttemptMs_ = now;

    const int serverSlot = recoveryServerSlot_;
    if (serverSlot >= 0 && serverSlot <= 8)
    {
        if (!packets::sendMobEquipment(player, serverSlot))
        {
            mServerSlot_ = -1; // unknown; force a fresh sync before placement
            return false;
        }
        mServerSlot_ = serverSlot;
    }
    finishSlotRecovery();
    MC_LOG("[Scaffold] recovered hotbar state after a placement fault");
    return true;
}

void Scaffold::recoverAfterTickFault(unsigned long code)
{
    const unsigned long long now = GetTickCount64();
    nextRetryMs_ = now + 250ull;
    if (now - lastExceptionLogMs_ >= 1000ull)
    {
        lastExceptionLogMs_ = now;
        MC_LOG_ERROR("[Scaffold] caught exception 0x%08lX; keeping module enabled and retrying",
                     code);
    }
    failStreak_ = 0;
    forcedBlockSlot_ = -1;
    mIsTowering_ = false;
    rot::clear(rot::PrioMovement);
    rotation_helper::clearClientRotation(rot::PrioMovement);
    // A fault may have interrupted the temporary Switch/Silent slot swap.
    // Keep its recovery record for the next safe tick.
}

// Puerto de Scaffold::tickPlace (Solstice).
bool Scaffold::tickPlace(const LocalPlayer& lp,
                         const BlockSource& bs)
{
    LocalPlayer& mlp = const_cast<LocalPlayer&>(lp);
    GameMode activeMode = Game::get().gameMode();
    const bool survivalMode = activeMode.isSurvival();

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

    // Select a block stack independently from the currently visible hand.
    // Verified block items take priority; retain the non-null Item* fallback
    // for block classes missing from this build's vtable list.
    Inventory inv = mlp.inventory();
    ItemInstance heldNow = mlp.heldItem();
    const int currentSlot = mlp.hotbarSlot();
    int blockSlot = -1;
    int bestBlockCount = 0;
    int fallbackSlot = -1;
    int fallbackCount = 0;
    if (inv.valid())
    {
        for (int slot = 0; slot <= 8; ++slot)
        {
            uintptr_t addr = inv.hotbarItem(slot);
            if (!addr)
                continue;
            ItemInstance item(addr);
            if (!hasReadableItem(item))
                continue;
            const int count = safeStackCount(item);
            if (fallbackSlot < 0 || count > fallbackCount)
            {
                fallbackSlot = slot;
                fallbackCount = count;
            }
            if (item.isBlockItem())
            {
                const int score = count + 1000000;
                if (blockSlot < 0 || score > bestBlockCount)
                {
                    blockSlot = slot;
                    bestBlockCount = score;
                }
            }
        }
    }
    if (blockSlot < 0)
        blockSlot = fallbackSlot;
    if (forcedBlockSlot_ >= 0 && forcedBlockSlot_ <= 8 && inv.hotbarItem(forcedBlockSlot_))
        blockSlot = forcedBlockSlot_;
    if (blockSlot < 0)
    {
        scDbg("no block stack in hotbar (held=0x%llX valid=%d)",
              (unsigned long long)heldNow.address(), heldNow.valid() ? 1 : 0);
        return false;
    }

    const int currentInventorySlot = inv.valid() ? inv.selectedSlot() : -1;
    if (placeMode_ == kPick &&
        blockSlot != currentInventorySlot)
    {
        // Pick is the real client-side slot selection. The native UseItem
        // packet carries this slot, so do not make placement depend on the
        // optional MobEquipment spoof packet (which can fail to resolve a
        // PacketSender in multiplayer sessions).
        mlp.setHotbarSlot(blockSlot);
        if (inv.valid())
            inv.setSelectedSlot(blockSlot);
        mServerSlot_ = blockSlot;
        scDbg("Pick selected slot %d (actor=%d inv=%d)",
              blockSlot, mlp.hotbarSlot(), inv.valid() ? inv.selectedSlot() : -1);
    }

    if (placeMode_ == kPick && currentSlot != blockSlot)
        mlp.setHotbarSlot(blockSlot);

    ItemInstance held(inv.hotbarItem(blockSlot));
    if (!hasReadableItem(held))
        return false;

    // Silent updates server equipment while preserving the local hand.
    if (placeMode_ == kSilent && mServerSlot_ != blockSlot)
    {
        if (!packets::sendMobEquipment(mlp, blockSlot))
            return false;
        mServerSlot_ = blockSlot;
    }

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
        if (rotateMode_ != 5)
        {
            const float clientYaw = reverseYaw(mlp, axisOf('S', 'W'), strafe);
            rotation_helper::requestClientRotation(mlp, clientYaw, 89.9f, rot::PrioMovement);
            rot::request(89.9f, clientYaw, 400, rot::PrioMovement);
        }
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

    // Match Solstice's getPlacePos flow: resolve a missing support around the
    // base first, then apply Extend only when that resolved target is already
    // occupied. Searching forward before resolving the base made Extend jump
    // past valid base placements and could select unsupported cells.
    const int desiredX = bx;
    const int desiredY = by;
    const int desiredZ = bz;
    bool targetResolved = getBlockPlaceFace(bs, bx, by, bz) >= 0;

    if (!targetResolved)
    {
        int rx, ry, rz;
        if (getClosestPlacePos(bs, desiredX, desiredY, desiredZ,
                               static_cast<int>(range_), rx, ry, rz))
        {
            bx = rx;
            by = ry;
            bz = rz;
            targetResolved = true;
        }
    }

    if (targetResolved && !isPlaceableCell(bs, bx, by, bz) &&
        maxExtend >= 1.0f && !mIsTowering_)
    {
        const int maxSteps = std::clamp(static_cast<int>(std::floor(maxExtend)), 0, 10);
        for (int step = 1; step <= maxSteps; ++step)
        {
            int ex, ey, ez;
            getRotBasedPos(mlp, static_cast<float>(step),
                           static_cast<float>(desiredY), ex, ey, ez);
            if (ex == bx && ey == by && ez == bz)
                continue; // fractional positions can map to the same block cell
            // Extend is directional: do not let the radial fallback pull the
            // candidate sideways or behind the player.
            if (!isPlaceableCell(bs, ex, ey, ez) ||
                getBlockPlaceFace(bs, ex, ey, ez) < 0)
                continue;

            bx = ex;
            by = ey;
            bz = ez;
            targetResolved = true;
            break; // nearest valid forward cell; don't overshoot the bridge
        }
    }

    if (!targetResolved)
    {
        scDbg("sin celda con soporte cerca de (%d,%d,%d) rango=%d",
              desiredX, desiredY, desiredZ, static_cast<int>(range_));
        return false;
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

    // Advanced uses the exact click point on the selected support face. This
    // request is made before the placement attempt so an airborne bridge can
    // align its server rotation with the face being targeted, even if this
    // tick's placement is rejected and retried on the next one.
    if (rotateMode_ == 5)
    {
        const Vec3 eye = mlp.eyePos();
        const float aimX = cx - eye.x;
        const float aimY = cy - eye.y;
        const float aimZ = cz - eye.z;
        const float horizontal = std::sqrt(aimX * aimX + aimZ * aimZ);
        float aimYaw = rad2deg(std::atan2(aimZ, aimX)) - 90.0f;
        float aimPitch = rad2deg(-std::atan2(aimY, horizontal));
        while (aimYaw > 180.0f)
            aimYaw -= 360.0f;
        while (aimYaw < -180.0f)
            aimYaw += 360.0f;
        rotation_helper::requestClientRotation(mlp, aimYaw, aimPitch, rot::PrioMovement);
        rot::request(aimPitch, aimYaw, 500, rot::PrioMovement);
    }

    // --- placeBlock (Solstice): el orquestador real del juego ---
    BlockPos support{sx, sy, sz};
    Vec3 clickAbs{cx, cy, cz};
    const int previousInventorySlot = inv.valid() ? inv.selectedSlot() : -1;
    const int previousActorSlot = mlp.hotbarSlot();
    const int switchReturnSlot = previousActorSlot;
    const int restoreServerSlot = placeMode_ == kSwitch
                                      ? switchReturnSlot
                                      : mServerSlot_;
    const bool temporarySlotChange = placeMode_ != kPick &&
                                     ((previousActorSlot != blockSlot) ||
                                      (previousInventorySlot != blockSlot) ||
                                      (placeMode_ == kSwitch && mServerSlot_ != blockSlot));
    if (temporarySlotChange)
        beginSlotRecovery(previousActorSlot, previousInventorySlot, restoreServerSlot);

    // Switch syncs equipment only around this placement. The server then sees
    // the normal hand again while the client selection never changes.
    if (placeMode_ == kSwitch && mServerSlot_ != blockSlot)
    {
        if (!packets::sendMobEquipment(mlp, blockSlot))
            return false;
        mServerSlot_ = blockSlot;
    }

    // The native routine reads both selection fields; keep them aligned with
    // the source stack for the synchronous packet/local-use operation.
    if (placeMode_ != kPick && previousActorSlot != blockSlot)
        mlp.setHotbarSlot(blockSlot);
    if (inv.valid() && previousInventorySlot != blockSlot)
        inv.setSelectedSlot(blockSlot);
    // Use one placement route in every game mode. The native GameMode
    // virtual path is unreliable in multiplayer Creative as well as
    // Survival, while the build's UseItemPacket constructor + gameplay sender
    // is the same server-facing route used by Bedrock's interaction flow.
    // Apply the item's local use once after sending so the client predicts the
    // block and local placement hooks observe it without a duplicate packet.
    const Vec3 packetPlayerPos = mlp.posExact();
    const int packetSlot = inv.valid() ? inv.selectedSlot() : blockSlot;
    const bool sent = sendPlacePacket(held, support, (uint8_t)face, clickAbs,
                                      packetPlayerPos, packetSlot);
    bool predicted = false;
    if (sent)
    {
        predicted = applyLocalBlockPlacement(
            held, mlp.address(), support, (uint8_t)face, clickAbs);
        scDbg("%s placement sent target=(%d,%d,%d) local=%d slot=%d",
              survivalMode ? "Survival" : "Creative",
              bx, by, bz, predicted ? 1 : 0, packetSlot);
    }
    const bool ok = sent;
    if (inv.valid() && previousInventorySlot >= 0 && previousInventorySlot <= 8 &&
        previousInventorySlot != blockSlot)
        inv.setSelectedSlot(previousInventorySlot);
    if (placeMode_ != kPick && previousActorSlot >= 0 && previousActorSlot <= 8 &&
        previousActorSlot != blockSlot)
        mlp.setHotbarSlot(previousActorSlot);

    if (placeMode_ == kSwitch && switchReturnSlot >= 0 && switchReturnSlot <= 8 &&
        switchReturnSlot != blockSlot)
    {
        if (packets::sendMobEquipment(mlp, switchReturnSlot))
            mServerSlot_ = switchReturnSlot;
        else
            mServerSlot_ = -1;
    }
    if (temporarySlotChange)
    {
        if (placeMode_ == kSwitch && mServerSlot_ != switchReturnSlot)
            slotRecoveryPending_ = true;
        else
            finishSlotRecovery();
    }

    if (!ok)
    {
        scDbg("useItemOn rejected target=(%d,%d,%d) support=(%d,%d,%d) face=%d slot=%d inv=%d mode=%d",
              bx, by, bz, sx, sy, sz, face, mlp.hotbarSlot(),
              inv.valid() ? inv.selectedSlot() : -1, placeMode_);
        // El orquestador valido y rechazo (colision con tu propio
        // hitbox, cooldown, item en estado raro). Como el v6/Solstice:
        // NADA de fallback de paquete manual — mandar un place que el
        // propio cliente acaba de rechazar crea bloques fantasma y
        // desync con el server. El proximo tick reintenta con la
        // geometria nueva; el failStreak hace el swap si el held esta
        // agotado.
        if (++failStreak_ >= 2)
        {
            int cur = blockSlot;
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
                int c = safeStackCount(it);
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
                forcedBlockSlot_ = best;
                scDbg("next block source slot %d (after failures on %d)", best, cur);
            }
            failStreak_ = 0;
        }
        return false;
    }
    failStreak_ = 0;
    forcedBlockSlot_ = -1;
    scDbg("useItemOn accepted target=(%d,%d,%d) support=(%d,%d,%d) face=%d slot=%d inv=%d mode=%d",
          bx, by, bz, sx, sy, sz, face, mlp.hotbarSlot(),
          inv.valid() ? inv.selectedSlot() : -1, placeMode_);

    // Actor::swing performs the local hand animation and emits the normal
    // AnimatePacket (action 1), so this checkbox covers both client and server.
    if (swing_)
        (void)mlp.swing();

    static ULONGLONG s_lastOk = 0;
    ULONGLONG now = GetTickCount64();
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
        Vec3 target = rotateMode_ == 5
                          ? clickAbs
                          : Vec3{
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

        if (mode_ == kTelly && tellyAir && rotateMode_ != 5)
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
            case 5: // Advanced: getRots exactos al punto de clic de la cara valida
                outYaw = calcYaw;
                outPitch = calcPitch;
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
    runTickGuarded();
}

void Scaffold::runTickGuarded()
{
    if (GetTickCount64() < nextRetryMs_)
        return;

    try
    {
        (void)sehScaffoldTick(this);
    }
    catch (const std::exception& e)
    {
        recoverAfterTickFault(0xE06D7363u);
        MC_LOG_ERROR("[Scaffold] C++ exception during tick: %s", e.what());
    }
    catch (...)
    {
        recoverAfterTickFault(0xE06D7363u);
        MC_LOG_ERROR("[Scaffold] unknown C++ exception during tick");
    }
}

void Scaffold::onTickImpl()
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

    // Recover an interrupted Switch/Silent transaction before another place
    // attempt can use a stale selected stack or server equipment slot.
    if (slotRecoveryPending_ && !recoverSlots(lp))
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
        {"placeMode", placeMode_},
        {"places", places_},
        {"extend", extend_},
        {"range", range_},
        {"lockY", lockY_},
        {"avoidUnderplace", avoidUnderplace_},
        {"towerMode", towerMode_},
        {"towerSpeed", towerSpeed_},
        {"allowMovement", allowMovement_},
        {"swing", swing_},
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
    placeMode_ = j.value("placeMode", placeMode_);
    if (placeMode_ < kPick || placeMode_ > kSilent)
        placeMode_ = kPick;
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
    swing_ = j.value("swing", swing_);
    rotateMode_ = j.value("rotateMode", rotateMode_);
    if (rotateMode_ < 0)
        rotateMode_ = 0;
    if (rotateMode_ > 5)
        rotateMode_ = 5;

    tellyLead_ = clampf(j.value("tellyLead", tellyLead_), 0.0f, 0.6f);
    rewindDelayMs_ = j.value("rewindDelayMs", rewindDelayMs_);
    if (rewindDelayMs_ < 0)
        rewindDelayMs_ = 0;
    if (rewindDelayMs_ > 250)
        rewindDelayMs_ = 250;
}

void Scaffold::drawSettings()
{
    if (ImGui::Button("Trace outgoing packets (8s)##sc_pkttrace"))
        chathook::armPacketTrace(8);
    ImGui::TextDisabled("Use while KillAura is attacking to inspect VInteract action and target ID");
    ImGui::Separator();

    const char* modes[] = {"Normal", "Telly", "RewindSide"};
    widgets::CCombo("Mode##sc", &mode_, modes, 3);
    const char* placeModes[] = {"Pick", "Switch", "Silent"};
    widgets::CCombo("Place mode##sc", &placeMode_, placeModes, 3);
    ImGui::TextDisabled("Pick selects the block slot; Switch keeps your hand; Silent spoofs server equipment");

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
    widgets::Checkbox("Swing##sc", &swing_);
    ImGui::Separator();
    const char* towers[] = {"Vanilla", "Velocity", "Clip"};
    widgets::CCombo("Tower mode##sc", &towerMode_, towers, 3);
    widgets::CSlider("Tower speed##sc", &towerSpeed_, 0.0f, 20.0f, "%.1f");
    widgets::Checkbox("Allow movement (tower)##sc", &allowMovement_);
    ImGui::Separator();
    const char* rots[] = {"None", "Normal", "Down", "Backwards", "Reverse", "Advanced"};
    widgets::CCombo("Rotate mode##sc", &rotateMode_, rots, 6);
    ImGui::TextDisabled("Silent rotations apply to movement packets and the local model");
    ImGui::TextDisabled("Smooth body yaw and head yaw/pitch; camera stays unchanged");
    ImGui::TextDisabled("Reverse = back to the placed face, pitch 89.9 (Telly look)");
    ImGui::TextDisabled("Advanced aims at the exact valid block face, including mid-air");
}

MC_REGISTER_MODULE(Scaffold);

}
