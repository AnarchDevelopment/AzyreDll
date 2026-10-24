#include "MotionBlur.hpp"
#include "GUI/Widgets.hpp"
#include "GUI/Menu.hpp"
#include "Render/BlurHelper/BlurHelper.hpp"

#include <algorithm>

namespace mc {

MotionBlur::MotionBlur()
    : Module("MotionBlur", "Applies motion blur to the game using EMA accumulation (arst-motion-blur)",
             Category::Visuals, 0)
{
    markHasSettings();
}

void MotionBlur::onEnable()
{
    blur::setMotionBlurEnabled(true);
    blur::setMotionBlurStrength(strength_ / 100.0f);
    blur::resetMotionBlur();
}

void MotionBlur::onDisable()
{
    blur::setMotionBlurEnabled(false);
    blur::resetMotionBlur();
}

void MotionBlur::onShutdown()
{
    blur::setMotionBlurEnabled(false);
    blur::resetMotionBlur();
}

void MotionBlur::onRender()
{
    // Sync settings in case strength changed:
    blur::setMotionBlurStrength(strength_ / 100.0f);
    // Note: No draw calls in ImGui draw lists!
    // MotionBlur is rendered directly on the D3D11 backbuffer before ImGui renders,
    // ensuring the blur only affects the game world and never smears widgets or GUI.
}

void MotionBlur::drawSettings()
{
    widgets::CSlider("Strength##mb", &strength_, 1.0f, 99.0f, "%.0f%%");
}

nlohmann::json MotionBlur::saveSettings() const
{
    return nlohmann::json{
        {"strength", strength_},
    };
}

void MotionBlur::loadSettings(const nlohmann::json& j)
{
    if (j.contains("strength"))
    {
        strength_ = j.value("strength", strength_);
    }
    else if (j.contains("intensity")) // backwards compatibility
    {
        strength_ = j.value("intensity", 0.75f) * 100.0f;
    }
    strength_ = std::clamp(strength_, 1.0f, 99.0f);
    if (enabled())
    {
        blur::setMotionBlurStrength(strength_ / 100.0f);
    }
}

MC_REGISTER_MODULE(MotionBlur);

}
