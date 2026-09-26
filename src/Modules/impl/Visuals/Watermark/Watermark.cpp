#include "Watermark.hpp"

#include "GUI/Theme.hpp"
#include "GUI/Widgets.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>
#include <cstdio>
#include <string>

namespace mc {

Watermark::Watermark()
    : Module("Watermark", "Watermark with FPS and player name", Category::Visuals, 0)
{
    setEnabled(true);
}

void Watermark::onRender()
{
    ImGuiIO& io = ImGui::GetIO();
    float sw = io.DisplaySize.x;
    float sh = io.DisplaySize.y;
    if (sw <= 1.0f || sh <= 1.0f)
        return;

    Game& game = Game::get();
    ImDrawList* d = ImGui::GetForegroundDrawList();
    ULONGLONG now = GetTickCount64();

    static std::string playerName;
    static ULONGLONG lastName = 0;
    if (now - lastName > 1000)
    {
        playerName = game.localFound() ? game.localPlayer().name() : "";
        lastName = now;
    }

    char buf[160];
    if (!playerName.empty())
        snprintf(buf, sizeof(buf), "AZYRE  |  %s  |  %.0f FPS", playerName.c_str(), io.Framerate);
    else
        snprintf(buf, sizeof(buf), "AZYRE  |  %.0f FPS", io.Framerate);

    ImVec2 ts = ImGui::CalcTextSize(buf);
    float pad = 9.0f;
    ImVec2 p0(12.0f, 12.0f);
    ImVec2 p1(12.0f + ts.x + pad * 2.0f + 5.0f, 12.0f + ts.y + pad * 2.0f);

    // Panel acrilico con blur en vivo + sombra.
    widgets::AcrylicPanel(d, p0, p1, 7.0f, 1.0f, sw, sh);
    d->AddRectFilled(ImVec2(p0.x + 2.0f, p0.y), ImVec2(p0.x + 4.0f, p1.y),
                     theme::AccentU32(1.0f));
    d->AddText(ImVec2(p0.x + pad + 5.0f, p0.y + pad), IM_COL32(240, 242, 245, 255), buf);
}

MC_REGISTER_MODULE(Watermark);

}
