#include "TargetHUD.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "Features/Friends.hpp"
#include "SDK/Game.hpp"

#include <cstdio>

namespace mc {

TargetHUD::TargetHUD()
    : Module("TargetHUD", "Info card of the player you are aiming at", Category::Visuals, 0)
{
    markHasSettings();
}

void TargetHUD::onRender()
{
    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 0.0f || sh <= 0.0f)
        return;

    Vec3 eye = local.eyePos();
    float myYaw = local.yaw();
    float myPitch = local.pitch();

    float bestAngle = 1e9f;
    const Actor* best = nullptr;
    float bestDist = 0.0f;

    for (const Actor& other : game.remotePlayers())
    {
        if (!other.valid())
            continue;

        Vec3 tEye = other.eyePos();
        float dist = distance3D(eye, tEye);
        if (dist > range_ || dist < 0.1f)
            continue;

        AimAngles a = calcAngles(eye, tEye);
        float yawDiff = angleDiff(myYaw, a.yaw);
        float pitchDiff = a.pitch - myPitch;
        if (std::fabs(yawDiff) > fov_ * 0.5f || std::fabs(pitchDiff) > fov_ * 0.5f)
            continue;

        float score = std::fabs(yawDiff) + std::fabs(pitchDiff);
        if (score < bestAngle)
        {
            bestAngle = score;
            best = &other;
            bestDist = dist;
        }
    }

    if (!best)
        return;

    std::string name = best->name();
    if (name.empty())
        name = "Player";
    bool fr = friends::isFriend(name);
    int hp = best->health();

    char title[96];
    if (showHealth_)
        snprintf(title, sizeof(title), "%s  [%d]", name.c_str(), hp);
    else
        snprintf(title, sizeof(title), "%s", name.c_str());

    char distBuf[32];
    snprintf(distBuf, sizeof(distBuf), "%.1fm", bestDist);

    ImVec2 ts = ImGui::CalcTextSize(title);
    ImVec2 ds = ImGui::CalcTextSize(distBuf);
    float w = (ts.x > ds.x ? ts.x : ds.x) + 40.0f;
    float h = 64.0f;
    float x0 = sw * 0.5f - w * 0.5f;
    float y0 = sh - 150.0f;

    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImU32 accent = fr ? friends::colorU32() : IM_COL32(214, 218, 226, 255);

    d->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), IM_COL32(10, 12, 15, 225), 8.0f);
    d->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + 4.0f, y0 + h), accent, 8.0f);

    d->AddText(ImVec2(x0 + 20.0f, y0 + 10.0f), IM_COL32(240, 242, 245, 255), title);
    d->AddText(ImVec2(x0 + 20.0f, y0 + 28.0f), IM_COL32(170, 175, 185, 255), distBuf);

    if (showHealth_)
    {
        float frac = clampf((float)hp / 20.0f, 0.0f, 1.0f);
        ImVec2 b0(x0 + 20.0f, y0 + h - 14.0f);
        ImVec2 b1(x0 + w - 20.0f, y0 + h - 8.0f);
        d->AddRectFilled(b0, b1, IM_COL32(0, 0, 0, 180), 3.0f);
        ImU32 hCol = IM_COL32((int)(255 * (1.0f - frac)), (int)(255 * frac), 40, 255);
        d->AddRectFilled(b0, ImVec2(b0.x + (b1.x - b0.x) * frac, b1.y), hCol, 3.0f);
    }
}

void TargetHUD::drawSettings()
{
    widgets::CSlider("Range##th", &range_, 2.0f, 16.0f, "%.1f");
    widgets::CSlider("FOV##th", &fov_, 5.0f, 120.0f, "%.0f");
    ImGui::Checkbox("Health##th", &showHealth_);
}

MC_REGISTER_MODULE(TargetHUD);

}
