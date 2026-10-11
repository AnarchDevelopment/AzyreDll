#include "MotionBlur.hpp"
#include "GUI/Widgets.hpp"

#include "GUI/Menu.hpp"
#include "Render/BlurHelper/BlurHelper.hpp"

#include <imgui.h>

#include <cmath>

namespace mc {

MotionBlur::MotionBlur()
    : Module("MotionBlur", "Applies a blur effect to the screen while moving",
             Category::Visuals, 0)
{
    markHasSettings();
}

void MotionBlur::onEnable()
{
    blur::setGhostRecording(true);
}

void MotionBlur::onDisable()
{
    blur::setGhostRecording(false);
}

void MotionBlur::onShutdown()
{
    blur::setGhostRecording(false);
}

void MotionBlur::onRender()
{
    // Nothing over the menu: the backdrop composite already covers the
    // screen and the ghosts would smear the widgets.
    if (menu::visible() || menu::animationAlpha() > 0.01f)
        return;

    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 1.0f || sh <= 1.0f)
        return;

    if (!blur::ghostRecording() || !blur::ghostFilled())
        return;

    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImVec2 screenMax(sw, sh);

    // Blend from OLDEST to NEWEST — exact port of D2D::ghostFrameCallback:
    // alpha starts at 0.3 * strength and multiplies by strength after each
    // ghost (NOT strength^age). With the Solstice defaults (0.9 strength,
    // 1 frame) the trail draws at 0.27 alpha — subtle, not a wall of ghost.
    int frames = blur::ghostFilled();
    if (frames > maxFrames_)
        frames = maxFrames_;

    float alpha = 0.3f * intensity_;
    for (int age = frames; age >= 1; --age)
    {
        void* srv = blur::ghostSrv(age);
        if (srv)
        {
            d->AddImage(ImTextureRef(srv), ImVec2(0.0f, 0.0f), screenMax,
                        ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                        IM_COL32(255, 255, 255, (int)(255.0f * alpha)));
        }
        alpha *= intensity_;
    }
}

void MotionBlur::drawSettings()
{
    widgets::CSlider("Max frames##mb", &maxFrames_, 1, 20);
    widgets::CSlider("Intensity##mb", &intensity_, 0.01f, 1.0f, "%.2f");
}

nlohmann::json MotionBlur::saveSettings() const
{
    return nlohmann::json{
        {"maxFrames", maxFrames_},
        {"intensity", intensity_},
    };
}

void MotionBlur::loadSettings(const nlohmann::json& j)
{
    maxFrames_ = j.value("maxFrames", maxFrames_);
    if (maxFrames_ < 1)
        maxFrames_ = 1;
    if (maxFrames_ > 20)
        maxFrames_ = 20;
    intensity_ = j.value("intensity", intensity_);
    if (intensity_ < 0.01f)
        intensity_ = 0.01f;
    if (intensity_ > 1.0f)
        intensity_ = 1.0f;
}

MC_REGISTER_MODULE(MotionBlur);

}
