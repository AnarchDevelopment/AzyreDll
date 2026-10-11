#pragma once

#include "Modules/Module.hpp"

namespace mc {

class ArrayList : public Module
{
public:
    ArrayList();

    void onRender() override;
    void drawSettings() override;

    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;

private:
    int style_ = 0;         // 0 = Azyre, 1 = Typical, 2 = Solstice
    int outlineMode_ = 0;    // 0 = Barra derecha, 1 = Barra izquierda, 2 = Borde full, 3 = Sin outline
    float bgOpacity_ = 0.82f;
    float barRounding_ = 2.0f;
    float barWidth_ = 3.0f;
    bool blurBg_ = true;

    // --- Modo Solstice ---
    int displayMode_ = 2;    // 0 = Outline, 1 = Bar, 2 = Split, 3 = Ninguno
    int visIdx_ = 0;         // 0 = Todos, 1 = Solo con keybind
    bool glow_ = true;
    float glowStrength_ = 1.0f;
    bool solShadow_ = true;   // mBackground: Shadow
    float solShadowOpacity_ = 1.0f; // mBackgroundOpacity
    float solBgValue_ = 0.0f;      // mBackgroundValue: 0 = negro, 1 = color de fila
    bool solBlur_ = false;
    bool solBold_ = true;    // mBoldText
    bool solFont_ = true;    // Product Sans Bold 26px (el font de Solstice)
    float fontSize_ = 20.0f;

    ImVec4 rowColor(size_t idx, size_t total);
    ImVec4 solsticeColor(float y) const;
};

}
