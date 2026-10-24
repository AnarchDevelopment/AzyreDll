#pragma once

#include "Modules/Module.hpp"

#include <string>

namespace mc {

class NameProtect : public Module
{
public:
    enum Style
    {
        kPlain = 0,
        kSolid = 1,
        kGradient = 2,
        kCustom = 3,
    };

    NameProtect();

    void onTick() override;
    void onDisable() override;
    void onShutdown() override;
    void drawSettings() override;

    std::string suffix() const override
    {
        return name_;
    }

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    // Genera el nombre final con codes (§X) segun el estilo, garantizando
    // que quepa en el presupuesto (SSO=15 o la capacidad heap actual).
    std::string generateStyled(size_t budget) const;
    void protectChat(const std::string& fake);
    void restore();
    void resetState();

    std::string name_ = "Protected";
    int style_ = kPlain;
    int color_ = 12;      // Red
    int gradFrom_ = 12;   // Red
    int gradTo_ = 14;     // Yellow
    std::string custom_ = "&cName";
    bool protectChat_ = true;

    // Nombre real aprendido del actor actual. El juego reescribe el nombre
    // de forma periodica (respawn, rejoin, nametags del servidor): todo
    // valor que no sea el fake actual (crudo o sin codes) se toma como el
    // real, se guarda para restaurar y se vuelve a tapar.
    uintptr_t actor_ = 0;
    std::string realName_;
    std::string realPlain_;
    std::string appliedFake_;
    int chatSkip_ = 0;
};

}
