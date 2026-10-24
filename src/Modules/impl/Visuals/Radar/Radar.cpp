#include "Radar.hpp"

#include "Config/Config.hpp"
#include "Features/Friends.hpp"
#include "Features/HudRegistry.hpp"
#include "Framework/Math.hpp"
#include "GUI/Theme.hpp"
#include "GUI/Widgets.hpp"
#include "Modules/ModuleManager.hpp"
#include "Modules/impl/Misc/StaffDetector/StaffDetector.hpp"
#include "Render/BlurHelper/BlurHelper.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>

#include <cmath>
#include <string>

namespace mc {

Radar::Radar()
    : Module("Radar", "2D radar with nearby players, friends and staff",
             Category::Visuals, 0)
{
    markHasSettings();
    setEnabled(true);
}

void Radar::onRender()
{
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 1.0f || sh <= 1.0f)
        return;

    Game& game = Game::get();
    if (!game.localFound())
        return;

    LocalPlayer local = game.localPlayer();
    Vec3 lp = local.pos();
    float yaw = local.yaw();

    StaffDetector* sd = nullptr;
    if (Module* m = ModuleManager::get().find("StaffDetector"))
        sd = static_cast<StaffDetector*>(m);

    float S = size_;
    ImVec2 panelSize(S, S);
    ImVec2 defPos(sw - S - 24.0f, 60.0f);
    hud::Registry::get().setConfigPath(config::baseDir() + "\\hud.json");
    ImVec2 pos = hud::Registry::get().report("Radar", defPos, panelSize);

    float cx = pos.x + S * 0.5f;
    float cy = pos.y + S * 0.5f;
    float R = S * 0.5f;

    ImDrawList* d = ImGui::GetForegroundDrawList();
    bool circle = (shape_ == 0);
    float round = circle ? R : 8.0f;
    ImVec2 rMin = pos;
    ImVec2 rMax(pos.x + S, pos.y + S);

    // --- Fondo ---
    if (blurBg_ && blur::wanted() && blur::ready() && blur::srv())
    {
        d->AddImageRounded(ImTextureRef((void*)blur::srv()), rMin, rMax,
                           ImVec2(rMin.x / sw, rMin.y / sh),
                           ImVec2(rMax.x / sw, rMax.y / sh),
                           IM_COL32(255, 255, 255, 255), round);
    }
    d->AddRectFilled(rMin, rMax, IM_COL32(16, 20, 28, (int)(178.0f)), round);
    d->AddRect(rMin, rMax, IM_COL32(255, 255, 255, 26), round, 0, 1.0f);

    // --- Grid: anillos y cruz ---
    ImU32 gridCol = IM_COL32(255, 255, 255, 16);
    if (circle)
    {
        d->AddCircle(ImVec2(cx, cy), R * (1.0f / 3.0f), gridCol, 32, 1.0f);
        d->AddCircle(ImVec2(cx, cy), R * (2.0f / 3.0f), gridCol, 40, 1.0f);
    }
    else
    {
        d->AddRect(ImVec2(rMin.x + S / 3.0f, rMin.y),
                   ImVec2(rMin.x + S / 3.0f, rMax.y), gridCol);
        d->AddRect(ImVec2(rMin.x + S * 2.0f / 3.0f, rMin.y),
                   ImVec2(rMin.x + S * 2.0f / 3.0f, rMax.y), gridCol);
        d->AddRect(ImVec2(rMin.x, rMin.y + S / 3.0f),
                   ImVec2(rMax.x, rMin.y + S / 3.0f), gridCol);
        d->AddRect(ImVec2(rMin.x, rMin.y + S * 2.0f / 3.0f),
                   ImVec2(rMax.x, rMin.y + S * 2.0f / 3.0f), gridCol);
    }
    d->AddLine(ImVec2(cx - R, cy), ImVec2(cx + R, cy), gridCol);
    d->AddLine(ImVec2(cx, cy - R), ImVec2(cx, cy + R), gridCol);

    // --- Blips de jugadores ---
    // Convencion del proyecto (Camera.hpp): forward XZ = (-sin(yaw), cos(yaw)).
    float yR = deg2rad(yaw);
    float fwdX = -sinf(yR), fwdZ = cosf(yR);
    float rightX = -cosf(yR), rightZ = -sinf(yR);

    ImFont* font = ImGui::GetFont();
    const float nameSz = 12.0f;

    d->PushClipRect(rMin, rMax, true);

    for (const Actor& other : game.remotePlayers())
    {
        if (!other.valid())
            continue;

        Vec3 p = other.pos();
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            continue;

        float dx = p.x - lp.x;
        float dz = p.z - lp.z;
        float dist = sqrtf(dx * dx + dz * dz);
        if (dist < 0.01f)
            continue;

        // Coordenadas de radar.
        float sX, sUp;
        if (rotate_)
        {
            sX = dx * rightX + dz * rightZ;
            sUp = dx * fwdX + dz * fwdZ;
        }
        else
        {
            // Norte (-Z) arriba.
            sX = dx;
            sUp = -dz;
        }

        float scale = R / range_;
        float vx = sX * scale;
        float vy = -sUp * scale;

        bool clamped = false;
        float vlen = sqrtf(vx * vx + vy * vy);
        if (vlen > R - 4.0f)
        {
            float k = (R - 4.0f) / vlen;
            vx *= k;
            vy *= k;
            clamped = true;
        }

        float bx = cx + vx;
        float by = cy + vy;

        // Color: friend (naranja de Friends), staff (rojo), resto blanco.
        std::string name = other.name();
        ImU32 col;
        if (friends::isFriend(name))
            col = friends::colorU32();
        else if (sd && sd->isStaff(name))
            col = IM_COL32(255, 80, 80, 255);
        else
            col = IM_COL32(230, 232, 238, 255);

        int alpha = clamped ? 140 : 255;
        col = (col & 0x00FFFFFFu) | ((unsigned)alpha << 24);

        d->AddCircleFilled(ImVec2(bx, by), 2.5f, col, 10);

        if (names_ && !name.empty() && !clamped)
        {
            float tsz = nameSz * 0.75f;
            ImVec2 ts = font->CalcTextSizeA(tsz, FLT_MAX, 0.0f, name.c_str());
            d->AddText(font, tsz, ImVec2(bx + 5.0f, by - ts.y * 0.5f),
                       (col & 0x00FFFFFFu) | 200u << 24, name.c_str());
        }
    }

    // --- Self: triangulo en el centro apuntando a donde miras ---
    {
        float ang = rotate_ ? -(float)M_PI / 2.0f : 0.0f;
        if (!rotate_)
        {
            // Modo fijo: girar el triangulo segun el yaw.
            float sfx = -sinf(yR), sfz = cosf(yR);
            ang = atan2f(-(-sfz), sfx); // angulo en pantalla del forward
        }
        float tri = 7.0f;
        ImVec2 v1(cx + cosf(ang) * tri, cy + sinf(ang) * tri);
        ImVec2 v2(cx + cosf(ang + 2.5f) * tri, cy + sinf(ang + 2.5f) * tri);
        ImVec2 v3(cx + cosf(ang - 2.5f) * tri, cy + sinf(ang - 2.5f) * tri);
        d->AddTriangleFilled(v1, v2, v3, theme::AccentU32());
    }

    d->PopClipRect();

    // --- Indicador "N" (solo en modo fijo, norte arriba) ---
    if (!rotate_)
    {
        const char* n = "N";
        ImVec2 ts = font->CalcTextSizeA(nameSz, FLT_MAX, 0.0f, n);
        d->AddText(font, nameSz, ImVec2(cx - ts.x * 0.5f, pos.y + 3.0f),
                   IM_COL32(255, 255, 255, 120), n);
    }
}

void Radar::drawSettings()
{
    const char* shapes[] = {"Circle", "Square"};
    widgets::CCombo("Shape##radar", &shape_, shapes, 2);
    widgets::CSlider("Range##radar", &range_, 16.0f, 256.0f, "%.0f blocks");
    widgets::CSlider("Size##radar", &size_, 80.0f, 300.0f, "%.0f");
    widgets::Checkbox("Rotate with player##radar", &rotate_);
    widgets::Checkbox("Show names##radar", &names_);
    widgets::Checkbox("Blur background##radar", &blurBg_);
}

nlohmann::json Radar::saveSettings() const
{
    return nlohmann::json{
        {"shape", shape_},
        {"range", range_},
        {"size", size_},
        {"rotate", rotate_},
        {"names", names_},
        {"blurBg", blurBg_},
    };
}

void Radar::loadSettings(const nlohmann::json& j)
{
    shape_ = j.value("shape", shape_);
    if (shape_ < 0 || shape_ > 1)
        shape_ = 0;
    range_ = j.value("range", range_);
    if (range_ < 16.0f)
        range_ = 16.0f;
    if (range_ > 256.0f)
        range_ = 256.0f;
    size_ = j.value("size", size_);
    if (size_ < 80.0f)
        size_ = 80.0f;
    if (size_ > 300.0f)
        size_ = 300.0f;
    rotate_ = j.value("rotate", rotate_);
    names_ = j.value("names", names_);
    blurBg_ = j.value("blurBg", blurBg_);
}

MC_REGISTER_MODULE(Radar);

}
