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
    int style_ = 0;         // 0 = Azyre, 1 = Typical, 2 = Solstice, 3 = Rise, 4 = Aurora
    int charStyle_ = 2;     // 0 = lowercase, 1 = lowercase spaced, 2 = Normalcase, 3 = Normalcase separated
    bool suffixes_ = true;
    bool advancedGradient_ = false;
    float gradientDirection_ = 0.0f;
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

    // --- Modo Rise (puerto de ModernInterface.java de Rise 6.9.5) ---
    int riseColorMode_ = 1;  // 0 = Static, 1 = Fade, 2 = Breathe (default Fade)
    int riseShaderEffect_ = 1; // 0 = Glow, 1 = Shadow (Rise Shader Effect)
    bool riseBg_ = true;     // Background: Normal
    bool riseShadow_ = true; // Drop Shadow
    bool riseSidebar_ = true;// Sidebar (barra de acento 2x9)
    bool riseBlur_ = true;   // Blur detras del fondo (panel esmerilado de Rise)
    float riseFontSize_ = 18.0f; // Rise usa Product Sans 18

    // --- Modo Aurora (preset propio: pildoras con barra de acento) ---
    int auColorMode_ = 2;    // 0 = Accent, 1 = Gradient por fila, 2 = Aurora (wave), 3 = White
    int auAnim_ = 0;         // 0 = Slide, 1 = Fade, 2 = Drop
    bool auBorder_ = true;   // borde de acento 1px
    bool auGlow_ = true;     // glow del texto/barra
    float auGlowStrength_ = 0.7f;
    bool auChip_ = true;     // sufijo como chip oscuro
    bool auBlur_ = true;     // blur de fondo disponible
    float auBgAlpha_ = 0.62f;   // opacidad del relleno oscuro
    float auPadding_ = 9.0f;    // padding horizontal
    float auGap_ = 3.0f;        // separacion entre pildoras
    float auRounding_ = 5.0f;   // redondeo
    float auFontSize_ = 18.0f;  // tamano de fuente

    ImVec4 rowColor(size_t idx, size_t total);
    ImVec4 solsticeColor(float y) const;
};

}
