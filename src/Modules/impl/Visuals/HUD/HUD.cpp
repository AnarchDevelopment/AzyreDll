#include "HUD.hpp"

#include "Framework/Math.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>
#include <cstdio>

namespace mc {

HUD::HUD()
    : Module("HUD", "Estadisticas del jugador en pantalla", Category::Visuals, 'H')
{
    markHasSettings();
}

static const char* directionFromYaw(float yaw)
{
    static const char* dirs[] = {"S", "SW", "W", "NW", "N", "NE", "E", "SE"};
    while (yaw < 0.0f) yaw += 360.0f;
    while (yaw >= 360.0f) yaw -= 360.0f;
    int idx = (int)((yaw + 22.5f) / 45.0f) % 8;
    return dirs[idx];
}

static void textShadow(ImDrawList* d, const ImVec2& pos, const char* text, ImU32 col)
{
    d->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f), IM_COL32(0, 0, 0, 200), text);
    d->AddText(pos, col, text);
}

void HUD::onRender()
{
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 1.0f || sh <= 1.0f)
        return;

    Game& game = Game::get();
    if (!stats_ || !game.localFound())
        return;

    ImDrawList* d = ImGui::GetForegroundDrawList();
    LocalPlayer local = game.localPlayer();
    Vec3 pos = local.pos();
    Vec3 vel = local.velocity();

    float lineH = 16.0f;
    float x = 14.0f;
    float y = sh - 16.0f;
    char lines[6][96];
    int count = 0;

    if (direction_)
        snprintf(lines[count++], 96, "Direction: %s   Yaw: %.0f", directionFromYaw(local.yaw()),
                 local.yaw());
    if (speed_)
        snprintf(lines[count++], 96, "Speed: %.2f b/s", vecLength2D(vel) * 20.0f);
    snprintf(lines[count++], 96, "XYZ: %.1f  %.1f  %.1f", pos.x, pos.y, pos.z);
    snprintf(lines[count++], 96, "Slot: %d/9   Hurt: %d", local.hotbarSlot() + 1,
             local.hurtTime());

    y -= (count - 1) * lineH;
    for (int i = 0; i < count; ++i)
    {
        ImVec2 ts = ImGui::CalcTextSize(lines[i]);
        d->AddRectFilled(ImVec2(x - 6.0f, y - 3.0f), ImVec2(x + ts.x + 8.0f, y + ts.y + 4.0f),
                         IM_COL32(10, 12, 15, 200), 5.0f);
        textShadow(d, ImVec2(x, y), lines[i], IM_COL32(235, 238, 240, 255));
        y += lineH;
    }
}

void HUD::drawSettings()
{
    ImGui::Checkbox("Stats##hud", &stats_);
    if (stats_)
    {
        ImGui::Indent(14.0f);
        ImGui::Checkbox("Speed##hud", &speed_);
        ImGui::SameLine();
        ImGui::Checkbox("Direction##hud", &direction_);
        ImGui::Unindent(14.0f);
    }
}

MC_REGISTER_MODULE(HUD);

}
