#pragma once

#include "Modules/Module.hpp"

namespace mc {

class LocalPlayer;

// Scaffold — coloca bloques automáticamente bajo los pies mientras te mueves.
// Usa el lector de chunks para detectar huecos y llama a blockItemUseOn
// (directo, sin hook) para colocar el bloque.
class Scaffold : public Module
{
public:
    enum class Mode { Stairs, Full, Down, Bottomless };

    Scaffold();

    void onEnable() override;
    void onDisable() override;
    void onTick() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    int bestSlot(const LocalPlayer& lp);

    int mode_ = 0; // 0=Stairs, 1=Full, 2=Down, 3=Bottomless
    bool onlyWhenFalling_ = false;
    int switchDelayMs_ = 50;
    unsigned long long lastSwapMs_ = 0;
    unsigned long long lastPlaceMs_ = 0;
    int placeDelayMs_ = 120;
};

}
