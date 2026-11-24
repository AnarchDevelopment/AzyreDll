#pragma once
#include "Modules/Module.hpp"

namespace mc {

// Selector global de estilo de GUI. El dispatch ocurre en menu::render().
namespace gui {

// 0 = Azyre, 1 = reserved legacy Solstice, 2 = Rise, 3 = Glass, 4 = Packet.
int style();
void setStyle(int s);

// Modo de blur del cliente:
//   0 = Native (BlurHelper en DX11)
//   1 = Solstice (Direct2D Gaussian blur)
//   2 = Rise (default: backdrop Gaussian con surface alpha y bloom claro)
int blurMode();
void setBlurMode(int mode);
float riseBlurOpacity();
void setRiseBlurOpacity(float opacity);

// Settings del estilo Solstice (persistidos por InterfaceModule). El tema
// de color (paletas Solstice) vive en Framework/ColorUtils (mc::colors).
int solsticeAnim(); // 0 = Zoom, 1 = Bounce
void setSolsticeAnim(int mode);
float solsticeEase();
void setSolsticeEase(float v);
float solsticeBlur(); // 0..10, alpha del backdrop
void setSolsticeBlur(float v);

// Settings del estilo Rise (persistidos por InterfaceModule):
float riseScale();
void setRiseScale(float v);

// Settings del estilo Glass.
float glassOpacity(); // 0.0 .. 1.0
void setGlassOpacity(float v);
int glassMotion(); // 0 = Fluid, 1 = Spring, 2 = Reduced
void setGlassMotion(int mode);

bool slotEasing();
void setSlotEasing(bool enabled);
bool installSlotEasingHook();
void uninstallSlotEasingHook();

} // namespace gui

class InterfaceModule : public Module
{
public:
    InterfaceModule();
    void drawSettings() override;
    nlohmann::json saveSettings() const override;
    void loadSettings(const nlohmann::json& j) override;
};

}
