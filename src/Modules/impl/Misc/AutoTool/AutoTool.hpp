#pragma once

#include "Modules/Module.hpp"

namespace mc {

class LocalPlayer;

enum class ToolKind
{
    None,
    Pickaxe,
    Axe,
    Shovel,
    Hoe,
};

class AutoTool : public Module
{
public:
    AutoTool();

    void onEnable() override;
    void onTick() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"reach", reach_},
            {"onlyMining", onlyMining_},
            {"switchBack", switchBack_},
            {"toolMode", toolMode_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        reach_ = j.value("reach", reach_);
        onlyMining_ = j.value("onlyMining", onlyMining_);
        switchBack_ = j.value("switchBack", switchBack_);
        toolMode_ = j.value("toolMode", toolMode_);
    }

private:
    int bestToolSlot(const LocalPlayer& lp, int blockId);

    // 0 = auto (clase correcta por bloque), 1 = cualquier herramienta.
    int toolMode_ = 0;
    float reach_ = 4.5f;
    bool onlyMining_ = true;
    bool switchBack_ = true;
    int prevSlot_ = -1;
};

}