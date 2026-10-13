#include "Scaffold.hpp"

#include "Features/WorldChunks.hpp"
#include "Framework/Math.hpp"
#include "GUI/Widgets.hpp"
#include "SDK/Classes/GameMode.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

#include <cmath>
#include <vector>

namespace mc {

Scaffold::Scaffold()
    : Module("Scaffold", "Automatically places blocks beneath you while moving",
             Category::Movement, 0)
{
    markHasSettings();
}

void Scaffold::onEnable()
{
    lastSwapMs_ = 0;
    lastPlaceMs_ = 0;
}

void Scaffold::onDisable()
{
}

int Scaffold::bestSlot(const LocalPlayer& lp)
{
    Inventory inv = lp.inventory();
    if (!inv.valid())
        return -1;

    int best = -1;
    int bestCount = 0;
    for (int slot = 0; slot <= 8; ++slot)
    {
        uintptr_t addr = inv.hotbarItem(slot);
        if (!addr)
            continue;
        ItemInstance item(addr);
        if (!item.valid() || item.count() <= 0)
            continue;
        if (!item.isBlockItem())
            continue;
        if (best == -1 || item.count() > bestCount)
        {
            best = slot;
            bestCount = item.count();
        }
    }
    return best;
}

void Scaffold::onTick()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer lp = game.localPlayer();
    if (!lp.valid())
        return;

    // Throttle: no intentar colocar cada tick.
    unsigned long long now = GetTickCount64();
    if (now - lastPlaceMs_ < (unsigned long long)placeDelayMs_)
        return;

    if (onlyWhenFalling_ && lp.velocity().y >= 0.0f)
        return;

    BlockSource bs = game.blockSource();
    if (!bs.valid())
        return;

    std::vector<world::ChunkRef> chunks;
    if (!world::enumerateChunks(bs, chunks))
        return;

    Vec3 feet = lp.pos();
    int fx = (int)std::floor(feet.x);
    int fy = (int)std::floor(feet.y);
    int fz = (int)std::floor(feet.z);

    if (fy < -64 || fy > 400)
        return;

    // Bloque bajo los pies.
    int standY = fy - 1;

    // Determinar el objetivo y la cara segun el modo.
    int clickX = 0, clickY = 0, clickZ = 0;
    uint8_t face = 1; // top por defecto

    switch (mode_)
    {
    case 0: // Stairs — colocar en la direccion de movimiento
    {
        float yaw = lp.yaw();
        float yR = deg2rad(yaw);
        Vec3 fwd{-std::sin(yR), 0.0f, std::cos(yR)};

        // Bloque objetivo: uno adelante a la altura de los pies.
        int tx = (int)std::floor(feet.x + fwd.x * 0.8f);
        int tz = (int)std::floor(feet.z + fwd.z * 0.8f);

        // Si ya hay bloque adelante a la altura de los pies, no hacer nada.
        int ahead = world::getBlockIdFromChunks(chunks, tx, fy, tz);
        if (ahead > 0)
            return;

        // Si hay bloque adelante abajo (para clickear la cara superior).
        int below = world::getBlockIdFromChunks(chunks, tx, fy - 1, tz);
        if (below > 0 && below != 8 && below != 10)
        {
            clickX = tx;
            clickY = fy - 1;
            clickZ = tz;
            face = 1; // top
        }
        else
            return;
        break;
    }
    case 1: // Full — colocar directamente debajo
    case 3: // Bottomless — igual que Full
    {
        int below = world::getBlockIdFromChunks(chunks, fx, standY, fz);
        // Si ya hay bloque debajo, no hacer nada.
        if (below > 0)
            return;
        // Necesitamos un bloque adyacente solido para clickear.
        // Probar los 4 lados del bloque debajo.
        const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        bool found = false;
        for (int i = 0; i < 4 && !found; ++i)
        {
            int nx = fx + dirs[i][0];
            int nz = fz + dirs[i][1];
            int side = world::getBlockIdFromChunks(chunks, nx, standY, nz);
            if (side > 0 && side != 8 && side != 10)
            {
                clickX = nx;
                clickY = standY;
                clickZ = nz;
                // Cara apuntando hacia el jugador (la cara del bloque
                // adyacente que mira al centro). Calculamos la cara segun
                // la direccion del bloque adyacente respecto al target.
                if (dirs[i][0] == 1) face = 4;      // west face del vecino
                else if (dirs[i][0] == -1) face = 5; // east face del vecino
                else if (dirs[i][1] == 1) face = 2;  // north face del vecino
                else face = 3;                        // south face del vecino
                found = true;
            }
        }
        if (!found)
            return;
        break;
    }
    case 2: // Down — colocar encima (para descender)
    {
        int above = world::getBlockIdFromChunks(chunks, fx, fy, fz);
        if (above > 0)
            return;
        int below = world::getBlockIdFromChunks(chunks, fx, standY, fz);
        if (below <= 0)
            return;
        clickX = fx;
        clickY = standY;
        clickZ = fz;
        face = 1; // top
        break;
    }
    default:
        return;
    }

    // Buscar slot con bloques.
    int slot = bestSlot(lp);
    if (slot < 0)
        return;

    // Cambiar slot si es necesario.
    if (slot != lp.hotbarSlot())
    {
        if (now - lastSwapMs_ < (unsigned long long)switchDelayMs_)
            return;
        lp.setHotbarSlot(slot);
        lastSwapMs_ = now;
        return; // esperar al proximo tick con el slot ya cambiado
    }

    // Verificar que el item sostenido es un bloque.
    ItemInstance held = lp.heldItem();
    if (!held.valid() || !held.isBlockItem())
        return;

    // Punto de clic: centro de la cara.
    float cx = 0.0f, cy = 0.0f, cz = 0.0f;
    switch (face)
    {
    case 0: // bottom
        cx = (float)clickX + 0.5f; cy = (float)clickY;       cz = (float)clickZ + 0.5f;
        break;
    case 1: // top
        cx = (float)clickX + 0.5f; cy = (float)clickY + 1.0f; cz = (float)clickZ + 0.5f;
        break;
    case 2: // north
        cx = (float)clickX + 0.5f; cy = (float)clickY + 0.5f; cz = (float)clickZ;
        break;
    case 3: // south
        cx = (float)clickX + 0.5f; cy = (float)clickY + 0.5f; cz = (float)clickZ + 1.0f;
        break;
    case 4: // west
        cx = (float)clickX;       cy = (float)clickY + 0.5f; cz = (float)clickZ + 0.5f;
        break;
    case 5: // east
        cx = (float)clickX + 1.0f; cy = (float)clickY + 0.5f; cz = (float)clickZ + 0.5f;
        break;
    default:
        return;
    }

    // Llamada directa a blockItemUseOn (sin hook — el hook de PlaceHighlights
    // ya reporta al ECS si el resultado es exito).
    BlockPos pos{clickX, clickY, clickZ};
    Vec3 click{cx, cy, cz};
    bool ok = mc::blockItemUseOn(held, pos, face, click, lp.address());

    if (ok)
        lastPlaceMs_ = now;
}

nlohmann::json Scaffold::saveSettings() const
{
    return nlohmann::json{
        {"mode", mode_},         {"onlyWhenFalling", onlyWhenFalling_},
        {"switchDelayMs", switchDelayMs_}, {"placeDelayMs", placeDelayMs_},
    };
}

void Scaffold::loadSettings(const nlohmann::json& j)
{
    mode_ = j.value("mode", mode_);
    onlyWhenFalling_ = j.value("onlyWhenFalling", onlyWhenFalling_);
    switchDelayMs_ = j.value("switchDelayMs", switchDelayMs_);
    placeDelayMs_ = j.value("placeDelayMs", placeDelayMs_);
}

void Scaffold::drawSettings()
{
    const char* modes[] = {"Stairs", "Full", "Down", "Bottomless"};
    widgets::CCombo("Mode##sc", &mode_, modes, 4);
    widgets::Checkbox("Only when falling##sc", &onlyWhenFalling_);
    widgets::CSlider("Place delay (ms)##sc", &placeDelayMs_, 50, 1000, "%d");
    widgets::CSlider("Switch delay (ms)##sc", &switchDelayMs_, 0, 500, "%d");
}

MC_REGISTER_MODULE(Scaffold);

}
