#include "BlockESP.hpp"

#include "Features/WorldChunks.hpp"
#include "Framework/Log.hpp"
#include "GUI/Widgets.hpp"
#include "SDK/Camera.hpp"
#include "SDK/Game.hpp"
#include "SDK/Offsets.hpp"
#include "SDK/VTables.hpp"

#include <imgui.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace mc {

namespace {

// ── IDs (legacy) ────────────────────────────────────────────────────────────
constexpr int ORE_DIAMOND = 56;
constexpr int ORE_EMERALD = 129;
constexpr int ORE_GOLD = 14;
constexpr int ORE_IRON = 15;
constexpr int ORE_COAL = 16;
constexpr int ORE_REDSTONE = 73;
constexpr int ORE_LAPIS = 21;
constexpr int BLK_CHEST = 54;
constexpr int BLK_ENDER_CHEST = 130;
constexpr int BLK_SPAWNER = 52;
constexpr int BLK_LAVA = 10;
constexpr int BLK_WATER = 8;

struct FaceQuad
{
    int c[4];
    Vec3 normal;
};

const FaceQuad kFaces[6] = {
    {{5, 7, 3, 1}, { 1.0f, 0.0f, 0.0f}},
    {{4, 6, 2, 0}, {-1.0f, 0.0f, 0.0f}},
    {{6, 7, 3, 2}, { 0.0f, 0.0f, 1.0f}},
    {{4, 5, 1, 0}, { 0.0f, 0.0f,-1.0f}},
    {{4, 5, 7, 6}, { 0.0f, 1.0f, 0.0f}},
    {{0, 2, 3, 1}, { 0.0f,-1.0f, 0.0f}},
};

const int kEdges[12][2] = {
    {0, 1}, {0, 2}, {0, 4}, {1, 3}, {1, 5}, {2, 3},
    {2, 6}, {3, 7}, {4, 5}, {4, 6}, {5, 7}, {6, 7},
};

} // namespace

BlockESP::BlockESP()
    : Module("BlockESP", "Draws a box around selected ores", Category::Visuals, 0)
{
    markHasSettings();
}

void BlockESP::onEnable()
{
    lastScan_ = 0;
    hits_.clear();
}

void BlockESP::onDisable()
{
    hits_.clear();
}

void BlockESP::buildTargets(std::vector<int>& out) const
{
    out.clear();
    if (mDiamond)  out.push_back(ORE_DIAMOND);
    if (mEmerald)  out.push_back(ORE_EMERALD);
    if (mGold)     out.push_back(ORE_GOLD);
    if (mIron)     out.push_back(ORE_IRON);
    if (mCoal)     out.push_back(ORE_COAL);
    if (mRedstone) out.push_back(ORE_REDSTONE);
    if (mLapis)    out.push_back(ORE_LAPIS);
    if (mChests)
    {
        out.push_back(BLK_CHEST);
        out.push_back(BLK_ENDER_CHEST);
    }
    if (mSpawners) out.push_back(BLK_SPAWNER);
    if (mLava)     out.push_back(BLK_LAVA);
    if (mWater)    out.push_back(BLK_WATER);
}

const char* BlockESP::oreName(int id)
{
    switch (id)
    {
    case ORE_DIAMOND: return "Diamond";
    case ORE_EMERALD: return "Emerald";
    case ORE_GOLD: return "Gold";
    case ORE_IRON: return "Iron";
    case ORE_COAL: return "Coal";
    case ORE_REDSTONE: return "Redstone";
    case ORE_LAPIS: return "Lapis";
    case BLK_LAVA: return "Lava";
    case BLK_WATER: return "Water";
    case BLK_CHEST: return "Chest";
    case BLK_ENDER_CHEST: return "Ender Chest";
    case BLK_SPAWNER: return "Spawner";
    default: return "Block";
    }
}

ImU32 BlockESP::colorFor(int id) const
{
    switch (id)
    {
    case ORE_DIAMOND: return IM_COL32(92, 219, 213, 255);
    case ORE_EMERALD: return IM_COL32(23, 200, 96, 255);
    case ORE_GOLD: return IM_COL32(252, 238, 75, 255);
    case ORE_IRON: return IM_COL32(216, 175, 147, 255);
    case ORE_COAL: return IM_COL32(64, 64, 64, 255);
    case ORE_REDSTONE: return IM_COL32(255, 60, 60, 255);
    case ORE_LAPIS: return IM_COL32(52, 88, 190, 255);
    case BLK_CHEST: return IM_COL32(255, 165, 0, 255);
    case BLK_ENDER_CHEST: return IM_COL32(150, 70, 200, 255);
    case BLK_SPAWNER: return IM_COL32(35, 35, 120, 255);
    case BLK_LAVA: return IM_COL32(255, 100, 0, 255);
    case BLK_WATER: return IM_COL32(50, 100, 255, 255);
    default: return IM_COL32(230, 230, 230, 255);
    }
}

void BlockESP::scan()
{
    hits_.clear();

    Game& game = Game::get();
    if (!game.localFound())
        return;

    BlockSource bs = game.blockSource();
    if (!bs.valid())
        return;

    std::vector<int> tv;
    buildTargets(tv);
    if (tv.empty())
        return;
    std::unordered_set<int> targets(tv.begin(), tv.end());

    std::vector<world::ChunkRef> chunks;
    if (!world::enumerateChunks(bs, chunks))
        return;

    Vec3 p = game.localPlayer().pos();
    int bx = (int)std::floor(p.x);
    int by = (int)std::floor(p.y);
    int bz = (int)std::floor(p.z);

    int xMin = bx - range_, xMax = bx + range_;
    int zMin = bz - range_, zMax = bz + range_;
    int yMin = by - range_;
    if (yMin < 0) yMin = 0;
    int yMax = by + range_;
    if (yMax > off::chunk::Height - 1) yMax = off::chunk::Height - 1;

    // Leer SOLO los chunks que intersectan el box (evita el lag de leer todo).
    struct CBuf
    {
        int cx, cz;
        const std::vector<uint8_t>* data;
    };
    std::vector<std::vector<uint8_t>> storage;
    std::unordered_map<uint32_t, size_t> index; // (cx,cz) -> storage idx
    storage.reserve(64);

    for (const world::ChunkRef& c : chunks)
    {
        int wx0 = c.cx * off::chunk::BoundsXZ;
        int wz0 = c.cz * off::chunk::BoundsXZ;
        if (wx0 + 15 < xMin || wx0 > xMax || wz0 + 15 < zMin || wz0 > zMax)
            continue;
        storage.emplace_back();
        storage.back().resize(off::chunk::Volume);
        if (!world::readChunkBlocks(c.addr, storage.back().data()))
        {
            storage.pop_back();
            continue;
        }
        uint32_t key = ((uint32_t)(uint16_t)c.cx << 16) | (uint16_t)c.cz;
        index[key] = storage.size() - 1;
    }
    if (storage.empty())
        return;

    auto bufAt = [&](int x, int z) -> const std::vector<uint8_t>* {
        uint32_t key = ((uint32_t)(uint16_t)(x >> 4) << 16) | (uint16_t)(z >> 4);
        auto it = index.find(key);
        if (it == index.end())
            return nullptr;
        return &storage[it->second];
    };
    auto blockIdAt = [&](int x, int y, int z) -> int {
        if (y < 0 || y > off::chunk::Height)
            return 0; // fuera del mundo = aire
        const std::vector<uint8_t>* b = bufAt(x, z);
        if (!b)
            return -1; // desconocido
        int idx = ((z & 15) + 16 * (x & 15)) * off::chunk::Height + y;
        return (*b)[idx];
    };
    auto isExposed = [&](const BlockPos& pos) -> bool {
        return blockIdAt(pos.x + 1, pos.y, pos.z) == 0 ||
               blockIdAt(pos.x - 1, pos.y, pos.z) == 0 ||
               blockIdAt(pos.x, pos.y + 1, pos.z) == 0 ||
               blockIdAt(pos.x, pos.y - 1, pos.z) == 0 ||
               blockIdAt(pos.x, pos.y, pos.z + 1) == 0 ||
               blockIdAt(pos.x, pos.y, pos.z - 1) == 0;
    };

    for (const auto& kv : index)
    {
        int cx = (int)(int16_t)(kv.first >> 16);
        int cz = (int)(int16_t)(kv.first & 0xFFFF);
        const std::vector<uint8_t>& buf = storage[kv.second];
        int wx0 = cx * off::chunk::BoundsXZ;
        int wz0 = cz * off::chunk::BoundsXZ;

        int xl0 = xMin - wx0; if (xl0 < 0) xl0 = 0;
        int xl1 = xMax - wx0; if (xl1 > 15) xl1 = 15;
        int zl0 = zMin - wz0; if (zl0 < 0) zl0 = 0;
        int zl1 = zMax - wz0; if (zl1 > 15) zl1 = 15;
        if (xl0 > xl1 || zl0 > zl1)
            continue;

        for (int xl = xl0; xl <= xl1; ++xl)
        {
            for (int zl = zl0; zl <= zl1; ++zl)
            {
                int col = (zl + 16 * xl) * off::chunk::Height;
                int wx = wx0 + xl;
                int wz = wz0 + zl;
                for (int y = yMin; y <= yMax; ++y)
                {
                    int id = buf[col + y];
                    if (id == 0 || targets.find(id) == targets.end())
                        continue;
                    BlockPos pos{wx, y, wz};
                    if (mOnlyExposed && !isExposed(pos))
                        continue;
                    Hit h;
                    h.pos = pos;
                    h.id = id;
                    hits_.push_back(h);
                    if ((int)hits_.size() >= maxBlocks_)
                        return;
                }
            }
        }
    }
}

void BlockESP::onTick()
{
    unsigned long long now = GetTickCount64();
    if (now - lastScan_ < (unsigned long long)refreshMs_)
        return;
    lastScan_ = now;
    scan();

    if (debug_)
        MC_LOG("[BlockESP] hits=%d", (int)hits_.size());
}

void BlockESP::onRender()
{
    Game& game = Game::get();
    if (!game.localFound() || hits_.empty())
        return;

    LocalPlayer lp = game.localPlayer();
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

    for (const Hit& h : hits_)
    {
        Vec3 corners[8];
        aabbCorners(h.pos, corners);

        Vec3 proj[8];
        bool ok[8];
        int validCount = 0;
        for (int i = 0; i < 8; ++i)
        {
            ok[i] = worldToScreen(corners[i], cam.pos, cam.yaw, cam.pitch, fov_, sw, sh, proj[i]);
            if (ok[i])
                ++validCount;
        }
        if (validCount == 0)
            continue; // totalmente detras de la camara

        ImU32 col = colorFor(h.id);

        // Caras (frente a la camara y con las 4 esquinas proyectadas).
        if (renderMode_ == 0 || renderMode_ == 2)
        {
            for (const FaceQuad& f : kFaces)
            {
                if (!(ok[f.c[0]] && ok[f.c[1]] && ok[f.c[2]] && ok[f.c[3]]))
                    continue;
                Vec3 center = (corners[f.c[0]] + corners[f.c[1]] + corners[f.c[2]] +
                               corners[f.c[3]]) * 0.25f;
                Vec3 toCam = cam.pos - center;
                if (f.normal.x * toCam.x + f.normal.y * toCam.y + f.normal.z * toCam.z <= 0.0f)
                    continue;
                ImVec2 pts[4] = {ImVec2(proj[f.c[0]].x, proj[f.c[0]].y),
                                 ImVec2(proj[f.c[1]].x, proj[f.c[1]].y),
                                 ImVec2(proj[f.c[2]].x, proj[f.c[2]].y),
                                 ImVec2(proj[f.c[3]].x, proj[f.c[3]].y)};
                d->AddConvexPolyFilled(pts, 4, (col & 0x00FFFFFF) | (60u << 24));
            }
        }

        // Aristas (solo las que tienen ambas esquinas proyectadas).
        if (renderMode_ == 1 || renderMode_ == 2)
        {
            for (auto& e : kEdges)
            {
                if (!(ok[e[0]] && ok[e[1]]))
                    continue;
                ImVec2 a(proj[e[0]].x, proj[e[0]].y);
                ImVec2 b(proj[e[1]].x, proj[e[1]].y);
                d->AddLine(a, b, IM_COL32(0, 0, 0, 200), 2.8f);
                d->AddLine(a, b, col, 1.6f);
            }
        }
    }
}

nlohmann::json BlockESP::saveSettings() const
{
    return nlohmann::json{
        {"diamond", mDiamond}, {"emerald", mEmerald}, {"gold", mGold}, {"iron", mIron},
        {"coal", mCoal},       {"redstone", mRedstone}, {"lapis", mLapis}, {"chests", mChests},
        {"spawners", mSpawners}, {"lava", mLava}, {"water", mWater},
        {"onlyExposed", mOnlyExposed}, {"renderMode", renderMode_},
        {"range", range_}, {"refreshMs", refreshMs_}, {"maxBlocks", maxBlocks_}, {"fov", fov_},
    };
}

void BlockESP::loadSettings(const nlohmann::json& j)
{
    mDiamond = j.value("diamond", mDiamond);
    mEmerald = j.value("emerald", mEmerald);
    mGold = j.value("gold", mGold);
    mIron = j.value("iron", mIron);
    mCoal = j.value("coal", mCoal);
    mRedstone = j.value("redstone", mRedstone);
    mLapis = j.value("lapis", mLapis);
    mChests = j.value("chests", mChests);
    mSpawners = j.value("spawners", mSpawners);
    mLava = j.value("lava", mLava);
    mWater = j.value("water", mWater);
    mOnlyExposed = j.value("onlyExposed", mOnlyExposed);
    renderMode_ = j.value("renderMode", renderMode_);
    range_ = j.value("range", range_);
    refreshMs_ = j.value("refreshMs", refreshMs_);
    maxBlocks_ = j.value("maxBlocks", maxBlocks_);
    fov_ = j.value("fov", fov_);
    lastScan_ = 0;
}

void BlockESP::drawSettings()
{
    const char* modes[] = {"Filled", "Outline", "Both"};
    widgets::CCombo("Render Mode##besp", &renderMode_, modes, 3);

    ImGui::SeparatorText("Ores");
    widgets::Checkbox("Diamond##besp", &mDiamond);
    ImGui::SameLine();
    widgets::Checkbox("Emerald##besp", &mEmerald);
    ImGui::SameLine();
    widgets::Checkbox("Gold##besp", &mGold);

    widgets::Checkbox("Iron##besp", &mIron);
    ImGui::SameLine();
    widgets::Checkbox("Coal##besp", &mCoal);
    ImGui::SameLine();
    widgets::Checkbox("Redstone##besp", &mRedstone);

    widgets::Checkbox("Lapis##besp", &mLapis);
    ImGui::SameLine();
    widgets::Checkbox("Chests##besp", &mChests);
    ImGui::SameLine();
    widgets::Checkbox("Spawners##besp", &mSpawners);

    widgets::Checkbox("Lava##besp", &mLava);
    ImGui::SameLine();
    widgets::Checkbox("Water##besp", &mWater);

    widgets::Checkbox("Only exposed ores##besp", &mOnlyExposed);

    ImGui::Separator();
    ImGui::SliderInt("Range##besp", &range_, 8, 128);
    ImGui::SliderInt("Update (ms)##besp", &refreshMs_, 200, 3000);
    ImGui::SliderInt("Max blocks##besp", &maxBlocks_, 100, 8000);
    ImGui::SliderFloat("FOV##besp", &fov_, 30.0f, 130.0f, "%.0f");
    ImGui::Checkbox("Debug##besp", &debug_);
}

MC_REGISTER_MODULE(BlockESP);

}
