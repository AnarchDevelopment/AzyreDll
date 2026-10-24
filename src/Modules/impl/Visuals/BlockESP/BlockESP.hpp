#pragma once

#include "Framework/Math.hpp"
#include "Modules/Module.hpp"

#include <imgui.h>

#include <cstdint>
#include <vector>

namespace mc {

// BlockESP — resalta minerales (por ID) leyendo los arrays de los LevelChunk.
// Estilo Solstice: checkboxes por mineral (diamante por defecto), colores por
// mineral y modo de render (Filled / Outline / Both).
class BlockESP : public Module
{
public:
    BlockESP();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void onRender() override;
    void drawSettings() override;

    std::string suffix() const override
    {
        static const char* modes[] = {"Filled", "Outline", "Both"};
        if (renderMode_ >= 0 && renderMode_ < 3)
            return modes[renderMode_];
        return "";
    }

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    struct Hit
    {
        BlockPos pos;
        int id = 0;
    };

    void scan();
    void buildTargets(std::vector<int>& out) const;
    ImU32 colorFor(int id) const;
    static const char* oreName(int id);

    std::vector<Hit> hits_;
    std::vector<uint8_t> blockBuf_;
    unsigned long long lastScan_ = 0;

    // Minerales (diamante por defecto).
    bool mDiamond = true;
    bool mEmerald = false;
    bool mGold = false;
    bool mIron = false;
    bool mCoal = false;
    bool mRedstone = false;
    bool mLapis = false;
    bool mChests = false;
    bool mSpawners = false;

    bool mOnlyExposed = false;
    int renderMode_ = 2; // 0 = Filled, 1 = Outline, 2 = Both

    bool mLava = false;
    bool mWater = false;

    int range_ = 48;
    int refreshMs_ = 1000;
    int maxBlocks_ = 1500;
    float fov_ = 110.0f;
    bool fovInited_ = false;
    bool debug_ = false;
};

}
