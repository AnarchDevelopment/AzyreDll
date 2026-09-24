#pragma once

#include "Modules/Module.hpp"

namespace mc {

class AntiBot : public Module
{
public:
    AntiBot();

    void onEnable() override;
    void onDisable() override;
    void drawSettings() override;
};

}
