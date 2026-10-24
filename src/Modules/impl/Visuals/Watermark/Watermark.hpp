#pragma once

#include "Modules/Module.hpp"

namespace mc {

class Watermark : public Module
{
public:
    Watermark();

    void onRender() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    void renderAzyre(ImDrawList* d, float sw, float sh, ImGuiIO& io);
    void renderSolstice(ImDrawList* d);
    void renderRise(ImDrawList* d);

    int style_ = 0;      // 0 = Azyre, 1 = Solstice, 2 = Rise
    bool glow_ = true;   // mGlow de Solstice
    float anim_ = 0.0f;   // slide/fade del modo Solstice
    char customName_[64] = ""; // Custom Client Name de Rise (vacio = nombre del jugador)
};

}
