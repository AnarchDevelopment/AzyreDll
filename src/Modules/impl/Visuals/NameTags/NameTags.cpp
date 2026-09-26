#include "NameTags.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "SDK/Camera.hpp"
#include "Features/Friends.hpp"
#include "SDK/Game.hpp"
#include "Features/Smoothing.hpp"

#include <cstdio>

namespace mc {

NameTags::NameTags()
    : Module("NameTags", "Floating name tags with health over players", Category::Visuals, 0)
{
    markHasSettings();
}

void NameTags::onRender()
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

    if (!fovInited_)
    {
        fov_ = readGameFov();
        fovInited_ = true;
    }

    CameraView cam = getCameraView(local, game.client());
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();

    for (const Actor& other : game.remotePlayers())
    {
        if (!other.valid())
            continue;

        std::string name = other.name();
        bool hasName = !name.empty();
        if (!hasName)
            name = "Player";
        bool fr = hasName && friends::isFriend(name);

        Vec3 feet = smooth::entityPos(other.address(), other.pos());
        float dist = distance3D(cam.pos, {feet.x, feet.y + 1.0f, feet.z});
        if (dist > maxDistance_ || dist < 0.5f)
            continue;

        float ht = other.height();
        Vec3 head{feet.x, feet.y + ht + 0.40f, feet.z};
        Vec3 s;
        if (!worldToScreen(head, cam.pos, cam.yaw, cam.pitch, fov_, sw, sh, s))
            continue;
        if (s.x < -150.0f || s.x > sw + 150.0f || s.y < -80.0f || s.y > sh + 80.0f)
            continue;

        float fs = fontSize_;
        if (scaleDistance_)
        {
            fs = fontSize_ * clampf(9.0f / dist, 0.60f, 1.70f);
            if (fs < 9.0f)
                fs = 9.0f;
        }

        int hp = other.health();
        char buf[96];
        if (showHealth_)
            snprintf(buf, sizeof(buf), "%s [%d]", name.c_str(), hp);
        else
            snprintf(buf, sizeof(buf), "%s", name.c_str());

        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, buf);
        float padX = 6.0f;
        float padY = 4.0f;
        ImVec2 c(s.x, s.y);
        ImVec2 p0(c.x - ts.x * 0.5f - padX, c.y - ts.y * 0.5f - padY);
        ImVec2 p1(c.x + ts.x * 0.5f + padX, c.y + ts.y * 0.5f + padY);

        d->AddRectFilled(p0, p1, IM_COL32(8, 10, 12, 195), 4.0f);
        if (fr)
            d->AddRectFilled(p0, ImVec2(p0.x + 3.0f, p1.y), friends::colorU32(), 4.0f);

        ImU32 col = fr ? friends::colorU32() : IM_COL32(240, 242, 245, 255);
        d->AddText(font, fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), col, buf);

        if (showHealth_)
        {
            float barW = ts.x + padX * 2.0f - 4.0f;
            float hFrac = clampf((float)hp / 20.0f, 0.0f, 1.0f);
            ImVec2 b0(p0.x + 2.0f, p1.y + 2.0f);
            ImVec2 b1(p0.x + 2.0f + barW, p1.y + 5.0f);
            d->AddRectFilled(b0, b1, IM_COL32(0, 0, 0, 170), 2.0f);
            ImU32 hCol = IM_COL32((int)(255 * (1.0f - hFrac)), (int)(255 * hFrac), 40, 255);
            d->AddRectFilled(b0, ImVec2(b0.x + barW * hFrac, b1.y), hCol, 2.0f);
        }
    }
}

void NameTags::drawSettings()
{
    ImGui::Checkbox("Health##nt", &showHealth_);
    ImGui::Checkbox("Scale with distance##nt", &scaleDistance_);
    widgets::CSlider("Font size##nt", &fontSize_, 10.0f, 24.0f, "%.0f");
    widgets::CSlider("Max distance##nt", &maxDistance_, 8.0f, 128.0f, "%.0f");
    widgets::CSlider("FOV##nt", &fov_, 30.0f, 130.0f, "%.0f");
}

MC_REGISTER_MODULE(NameTags);

}
