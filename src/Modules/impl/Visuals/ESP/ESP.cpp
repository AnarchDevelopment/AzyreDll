#include "ESP.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "SDK/Camera.hpp"
#include "Features/Friends.hpp"
#include "SDK/Game.hpp"
#include "Features/Smoothing.hpp"

namespace mc {

ESP::ESP()
    : Module("ESP", "Dibuja cajas sobre los jugadores", Category::Visuals, 'P')
{
    markHasSettings();
}

void ESP::onRender()
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

    auto drawLabels = [&](float cx, float yTop, const std::string& n, bool fr, float dist) {
        float textY = yTop - 4.0f;
        if (name_ && !n.empty())
        {
            ImVec2 ts = ImGui::CalcTextSize(n.c_str());
            ImU32 nameCol = fr ? friends::colorU32() : IM_COL32_WHITE;
            d->AddText(ImVec2(cx - ts.x * 0.5f, textY - ts.y), nameCol, n.c_str());
            textY -= ts.y + 2.0f;
        }
        if (distance_)
        {
            char db[32];
            snprintf(db, sizeof(db), "%.1fm", dist);
            ImVec2 ts = ImGui::CalcTextSize(db);
            d->AddText(ImVec2(cx - ts.x * 0.5f, textY - ts.y),
                       IM_COL32(245, 245, 245, 230), db);
        }
    };

    for (const Actor& other : game.remotePlayers())
    {
        if (!other.valid())
            continue;

        std::string n = other.name();
        bool fr = friends::isFriend(n);
        ImU32 bC = fr ? friends::colorU32() : ImGui::ColorConvertFloat4ToU32(color_);

        Vec3 feet = smooth::entityPos(other.address(), other.pos());
        float ht = other.height();
        float dist = distance3D(cam.pos, {feet.x, feet.y + 1.0f, feet.z});
        if (dist < 0.5f)
            continue;

        if (style_ == 1)
        {
            float hw = off::gameconst::PlayerWidth * 0.5f;
            Vec3 corners[8];
            int idx = 0;
            for (int yi = 0; yi <= 1; ++yi)
            {
                for (int zi = 0; zi <= 1; ++zi)
                {
                    for (int xi = 0; xi <= 1; ++xi)
                    {
                        corners[idx++] = {
                            feet.x + (xi ? hw : -hw),
                            feet.y + (yi ? ht : 0.0f),
                            feet.z + (zi ? hw : -hw),
                        };
                    }
                }
            }

            Vec3 proj[8];
            bool visible = true;
            for (int i = 0; i < 8; ++i)
            {
                if (!worldToScreen(corners[i], cam.pos, cam.yaw, cam.pitch, fov_, sw, sh, proj[i]))
                {
                    visible = false;
                    break;
                }
            }
            if (!visible)
                continue;

            static const int edges[12][2] = {
                {0, 1}, {0, 2}, {0, 4}, {1, 3}, {1, 5}, {2, 3},
                {2, 6}, {3, 7}, {4, 5}, {4, 6}, {5, 7}, {6, 7},
            };

            if (box_)
            {
                for (auto& e : edges)
                {
                    d->AddLine(ImVec2(proj[e[0]].x, proj[e[0]].y),
                               ImVec2(proj[e[1]].x, proj[e[1]].y),
                               IM_COL32(0, 0, 0, 200), 3.0f);
                }
                for (auto& e : edges)
                {
                    d->AddLine(ImVec2(proj[e[0]].x, proj[e[0]].y),
                               ImVec2(proj[e[1]].x, proj[e[1]].y), bC, 1.5f);
                }
            }

            Vec3 top{feet.x, feet.y + ht, feet.z};
            Vec3 sTop;
            if (worldToScreen(top, cam.pos, cam.yaw, cam.pitch, fov_, sw, sh, sTop))
                drawLabels(sTop.x, sTop.y, n, fr, dist);

            continue;
        }

        Vec3 sFeet, sHead;
        if (!worldToScreen(feet, cam.pos, cam.yaw, cam.pitch, fov_, sw, sh, sFeet))
            continue;
        Vec3 head{feet.x, feet.y + ht, feet.z};
        if (!worldToScreen(head, cam.pos, cam.yaw, cam.pitch, fov_, sw, sh, sHead))
            continue;

        float bH = sFeet.y - sHead.y;
        if (bH < 2.0f || bH > sh * 2.0f)
            continue;
        float bW = bH * 0.45f;
        float cX = (sFeet.x + sHead.x) * 0.5f;
        float x1 = cX - bW * 0.5f, y1 = sHead.y;
        float x2 = cX + bW * 0.5f, y2 = sFeet.y;

        if (y2 < -50.0f || y1 > sh + 50.0f)
            continue;
        if (x2 < -200.0f || x1 > sw + 200.0f)
            continue;

        if (box_)
        {
            d->AddRect(ImVec2(x1 - 1, y1 - 1), ImVec2(x2 + 1, y2 + 1), IM_COL32(0, 0, 0, 220), 0, 0, 2.5f);
            d->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), bC, 0, 0, 1.5f);
        }

        drawLabels(cX, y1, n, fr, dist);
    }
}

void ESP::drawSettings()
{
    const char* styles[] = {"2D", "3D"};
    widgets::CCombo("Style##esp", &style_, styles, 2);
    ImGui::Checkbox("Box##esp", &box_);
    ImGui::Checkbox("Name##esp", &name_);
    ImGui::Checkbox("Distance##esp", &distance_);
    widgets::CSlider("FOV##esp", &fov_, 30.0f, 130.0f, "%.0f");
    ImGui::ColorEdit4("Color##esp", (float*)&color_);
}

MC_REGISTER_MODULE(ESP);

}
