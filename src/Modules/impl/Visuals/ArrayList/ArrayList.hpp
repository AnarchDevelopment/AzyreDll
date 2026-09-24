#pragma once

#include "Modules/Module.hpp"

namespace mc {

class ArrayList : public Module
{
public:
    ArrayList();

    void onRender() override;
    void drawSettings() override;
};

}
