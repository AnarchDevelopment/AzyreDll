#include "Trail.hpp"
#include "GUI/Widgets.hpp"

#include "SDK/Camera.hpp"
#include "SDK/Game.hpp"

#include <algorithm>

namespace mc {

Trail::Trail()
    : Module("Trail", "Estela tras el jugador", Category::Visuals, 0)
{
    markHasSettings();
}

void Trail::onEnable()
{
    hist_.clear();
    lastPush_ = 0;
}

void Trail::onDisable()
{
    hist_.clear();
}

void Trail::onRender()
{
    Game& game = Game::get();
    if (!game.localFound())
    {
        hist_.clear();
        return;
    }

    LocalPlayer local = game.localPlayer();
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 0.0f || sh <= 0.0f)
        return;

    ULONGLONG now = GetTickCount64();
    if (now - lastPush_ >= 40)
    {
        Vec3 p = local.pos();
        p.y += 0.05f;
        hist_.push_back({p, now});
        lastPush_ = now;
    }

    ULONGLONG lenMs = (ULONGLONG)(lengthSec_ * 1000.0f);
    while (!hist_.empty() && now - hist_.front().time > lenMs)
        hist_.erase(hist_.begin());
    if (hist_.size() > 150)
        hist_.erase(hist_.begin(), hist_.end() - 150);

    if (hist_.size() < 2)
        return;

    if (!fovInited_)
    {
        fov_ = readGameFov();
        fovInited_ = true;
    }

    CameraView cam = getCameraView(local, game.client());
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImU32 baseCol = ImGui::ColorConvertFloat4ToU32(color_);

    size_t n = hist_.size();
    for (size_t i = 1; i < n; ++i)
    {
        Vec3 s0, s1;
        if (!worldToScreen(hist_[i - 1].pos, cam.pos, cam.yaw, cam.pitch, fov_, sw, sh, s0))
            continue;
        if (!worldToScreen(hist_[i].pos, cam.pos, cam.yaw, cam.pitch, fov_, sw, sh, s1))
            continue;

        float frac = (float)i / (float)(n - 1);
        float a = frac * (float)((baseCol >> 24) & 0xFF);
        ImU32 col = (baseCol & 0x00FFFFFFu) | ((UCHAR)a << 24);
        d->AddLine(ImVec2(s0.x, s0.y), ImVec2(s1.x, s1.y), col, thickness_);
    }
}

void Trail::drawSettings()
{
    widgets::CSlider("Length##trl", &lengthSec_, 0.5f, 4.0f, "%.1f s");
    widgets::CSlider("Thickness##trl", &thickness_, 0.5f, 4.0f, "%.1f");
    widgets::CSlider("FOV##trl", &fov_, 30.0f, 130.0f, "%.0f");
    ImGui::ColorEdit4("Color##trl", (float*)&color_);
}

MC_REGISTER_MODULE(Trail);

}
