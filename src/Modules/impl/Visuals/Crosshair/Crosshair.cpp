#include "Crosshair.hpp"
#include "GUI/Widgets.hpp"

namespace mc {

Crosshair::Crosshair()
    : Module("Crosshair", "Puntero personalizado en el centro", Category::Visuals, 0)
{
    markHasSettings();
}

void Crosshair::onRender()
{
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 0.0f || sh <= 0.0f)
        return;

    float cx = sw * 0.5f;
    float cy = sh * 0.5f;
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImU32 col = ImGui::ColorConvertFloat4ToU32(color_);
    ImU32 outline = IM_COL32(0, 0, 0, 190);

    auto drawArms = [&](ImU32 c, float thickness) {
        float g = gap_;
        float e = gap_ + size_;
        d->AddLine(ImVec2(cx, cy - e), ImVec2(cx, cy - g), c, thickness);
        d->AddLine(ImVec2(cx, cy + g), ImVec2(cx, cy + e), c, thickness);
        d->AddLine(ImVec2(cx - e, cy), ImVec2(cx - g, cy), c, thickness);
        d->AddLine(ImVec2(cx + g, cy), ImVec2(cx + e, cy), c, thickness);
    };

    if (outline_)
        drawArms(outline, thickness_ + 2.0f);
    drawArms(col, thickness_);

    if (dot_)
    {
        if (outline_)
            d->AddCircleFilled(ImVec2(cx, cy), thickness_ * 1.4f, outline);
        d->AddCircleFilled(ImVec2(cx, cy), thickness_, col);
    }
}

void Crosshair::drawSettings()
{
    widgets::CSlider("Size##ch", &size_, 2.0f, 20.0f, "%.0f");
    widgets::CSlider("Gap##ch", &gap_, 0.0f, 12.0f, "%.0f");
    widgets::CSlider("Thickness##ch", &thickness_, 1.0f, 5.0f, "%.0f");
    ImGui::Checkbox("Dot##ch", &dot_);
    ImGui::Checkbox("Outline##ch", &outline_);
    ImGui::ColorEdit4("Color##ch", (float*)&color_);
}

MC_REGISTER_MODULE(Crosshair);

}
