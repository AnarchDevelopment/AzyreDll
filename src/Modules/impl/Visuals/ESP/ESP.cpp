#include "ESP.hpp"
#include "GUI/Widgets.hpp"

#include "Framework/Math.hpp"
#include "SDK/Camera.hpp"
#include "Features/Friends.hpp"
#include "SDK/Game.hpp"
#include "Features/Smoothing.hpp"

namespace mc {

namespace {

struct FaceQuad
{
    int c[4];
    Vec3 normal;
    float shade;
    bool horizontal;
};

// Caras en orden [topA, topB, botB, botA] para el degradado vertical.
const FaceQuad kFaces[6] = {
    {{5, 7, 3, 1}, { 1.0f, 0.0f, 0.0f}, 0.85f, false}, // +X
    {{4, 6, 2, 0}, {-1.0f, 0.0f, 0.0f}, 0.85f, false}, // -X
    {{6, 7, 3, 2}, { 0.0f, 0.0f, 1.0f}, 0.70f, false}, // +Z
    {{4, 5, 1, 0}, { 0.0f, 0.0f,-1.0f}, 0.70f, false}, // -Z
    {{4, 5, 7, 6}, { 0.0f, 1.0f, 0.0f}, 1.00f, true},  // +Y
    {{0, 2, 3, 1}, { 0.0f,-1.0f, 0.0f}, 0.60f, true},  // -Y
};

void espBaseRgb(bool friendCol, const ImVec4& baseCol, int& r, int& g, int& b)
{
    if (friendCol)
    {
        ImU32 c = friends::colorU32();
        r = (int)(c & 0xFF);
        g = (int)((c >> 8) & 0xFF);
        b = (int)((c >> 16) & 0xFF);
    }
    else
    {
        r = (int)(baseCol.x * 255.0f);
        g = (int)(baseCol.y * 255.0f);
        b = (int)(baseCol.z * 255.0f);
    }
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
}

void gradientQuad(ImDrawList* d, const ImVec2 pts[4], const ImU32 cols[4])
{
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    d->PrimReserve(6, 4);
    ImDrawIdx base = (ImDrawIdx)d->_VtxCurrentIdx;
    for (int i = 0; i < 4; ++i)
        d->PrimWriteVtx(pts[i], uv, cols[i]);
    d->PrimWriteIdx(base);
    d->PrimWriteIdx((ImDrawIdx)(base + 1));
    d->PrimWriteIdx((ImDrawIdx)(base + 2));
    d->PrimWriteIdx(base);
    d->PrimWriteIdx((ImDrawIdx)(base + 2));
    d->PrimWriteIdx((ImDrawIdx)(base + 3));
}

void fillEspBox3D(ImDrawList* d, const Vec3 corners[8], const Vec3 proj[8],
                  const Vec3& camPos, bool friendCol, const ImVec4& baseCol,
                  float alpha, bool gradient)
{
    int r = 0, g = 0, b = 0;
    espBaseRgb(friendCol, baseCol, r, g, b);

    int aTop = (int)(alpha * 255.0f);
    if (aTop < 0) aTop = 0;
    if (aTop > 255) aTop = 255;
    int aBot = (int)(alpha * 255.0f * 0.05f);
    if (aBot < 0) aBot = 0;

    for (const FaceQuad& f : kFaces)
    {
        Vec3 center = (corners[f.c[0]] + corners[f.c[1]] + corners[f.c[2]] + corners[f.c[3]]) * 0.25f;
        Vec3 toCam = camPos - center;
        if (f.normal.x * toCam.x + f.normal.y * toCam.y + f.normal.z * toCam.z <= 0.0f)
            continue;

        ImVec2 pts[4] = {
            ImVec2(proj[f.c[0]].x, proj[f.c[0]].y),
            ImVec2(proj[f.c[1]].x, proj[f.c[1]].y),
            ImVec2(proj[f.c[2]].x, proj[f.c[2]].y),
            ImVec2(proj[f.c[3]].x, proj[f.c[3]].y),
        };

        int sr = (int)(r * f.shade);
        int sg = (int)(g * f.shade);
        int sb = (int)(b * f.shade);

        if (f.horizontal || !gradient)
        {
            d->AddConvexPolyFilled(pts, 4, IM_COL32(sr, sg, sb, aTop));
        }
        else
        {
            ImU32 cols[4] = {
                IM_COL32(sr, sg, sb, aTop), IM_COL32(sr, sg, sb, aTop),
                IM_COL32(sr, sg, sb, aBot), IM_COL32(sr, sg, sb, aBot),
            };
            gradientQuad(d, pts, cols);
        }
    }
}

void fillEspRect(ImDrawList* d, const ImVec2& a, const ImVec2& b, bool friendCol,
                 const ImVec4& baseCol, float alpha, bool gradient)
{
    int r = 0, g = 0, bl = 0;
    espBaseRgb(friendCol, baseCol, r, g, bl);

    int aTop = (int)(alpha * 255.0f);
    if (aTop < 0) aTop = 0;
    if (aTop > 255) aTop = 255;

    ImU32 top = IM_COL32(r, g, bl, aTop);
    if (gradient)
    {
        int aBot = (int)(alpha * 255.0f * 0.05f);
        if (aBot < 0) aBot = 0;
        ImU32 bot = IM_COL32(r, g, bl, aBot);
        d->AddRectFilledMultiColor(a, b, top, top, bot, bot);
    }
    else
    {
        d->AddRectFilled(a, b, top);
    }
}

}

ESP::ESP()
    : Module("ESP", "Draws boxes over players", Category::Visuals, 'P')
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

            if (filled_)
                fillEspBox3D(d, corners, proj, cam.pos, fr, color_, fillAlpha_, gradient_);

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

        if (filled_)
            fillEspRect(d, ImVec2(x1, y1), ImVec2(x2, y2), fr, color_,
                        fillAlpha_, gradient_);

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
    if (ImGui::Checkbox("Filled##esp", &filled_))
    {
    }
    if (filled_)
    {
        ImGui::Indent();
        ImGui::Checkbox("Gradient##esp", &gradient_);
        widgets::CSlider("Fill opacity##esp", &fillAlpha_, 0.05f, 0.80f, "%.2f");
        ImGui::Unindent();
    }
    widgets::CSlider("FOV##esp", &fov_, 30.0f, 130.0f, "%.0f");
    ImGui::ColorEdit4("Color##esp", (float*)&color_);
}

MC_REGISTER_MODULE(ESP);

}
