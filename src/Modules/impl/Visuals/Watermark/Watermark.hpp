#pragma once

#include "Modules/Module.hpp"

namespace mc {

class Watermark : public Module
{
public:
    Watermark();

    void onRender() override;
};

}
