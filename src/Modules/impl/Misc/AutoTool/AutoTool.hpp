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
    void onDisable() override;
    void onTick() override;
    void drawSettings() override;

    std::string suffix() const override
    {
        return toolMode_ == 0 ? "Auto" : "Any";
    }

    nlohmann::json saveSettings() const override
    {
        return nlohmann::json{
            {"reach", reach_},
            {"onlyMining", onlyMining_},
            {"switchBack", switchBack_},
            {"toolMode", toolMode_},
            {"syncServer", syncServer_},
            {"keepCurrent", keepCurrent_},
        };
    }

    void loadSettings(const nlohmann::json& j) override
    {
        reach_ = j.value("reach", reach_);
        onlyMining_ = j.value("onlyMining", onlyMining_);
        switchBack_ = j.value("switchBack", switchBack_);
        toolMode_ = j.value("toolMode", toolMode_);
        syncServer_ = j.value("syncServer", syncServer_);
        keepCurrent_ = j.value("keepCurrent", keepCurrent_);
    }

private:
    int bestToolSlot(const LocalPlayer& lp, int blockId);
    int toolTierAt(const LocalPlayer& lp, int hotbarSlot, ToolKind& kind) const;
    bool switchTo(LocalPlayer& lp, int slot);

    // 0 = auto (clase correcta por bloque), 1 = cualquier herramienta.
    int toolMode_ = 0;
    float reach_ = 4.5f;
    bool onlyMining_ = true;
    bool switchBack_ = true;
    // Envia MobEquipmentPacket al cambiar de slot. Sin esto el cliente cambia la
    // mano en local pero el servidor sigue viendo el slot anterior y rechaza
    // los places/roturas (mismo mecanismo que usa Scaffold).
    bool syncServer_ = true;
    // No cambiar si la herramienta en mano ya es del tipo correcto.
    bool keepCurrent_ = true;
    int prevSlot_ = -1;
    int serverSlot_ = -1;
};

}