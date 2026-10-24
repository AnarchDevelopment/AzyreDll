#include "AutoClicker.hpp"

#include "GUI/Menu.hpp"
#include "GUI/Widgets.hpp"
#include "Input/SendClick.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

namespace mc {

AutoClicker::AutoClicker()
    : Module("AutoClicker", "Automatically clicks at a configurable CPS", Category::Combat, 0)
{
    markHasSettings();
}

void AutoClicker::onTick()
{
    if (mc::menu::visible() || ImGui::GetIO().WantCaptureMouse)
        return;

    Game& game = Game::get();
    if (!game.localFound())
        return;

    bool leftHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    bool rightHeld = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;

    ULONGLONG now = GetTickCount64();
    int delay = cps_ > 0 ? (1000 / cps_) : 1000;

    if (left_ && leftHeld)
    {
        if (now - lastClick_ >= (ULONGLONG)delay)
        {
            sendLeftUp();
            sendLeftDown();
            lastClick_ = now;
        }
    }
    else if (right_ && rightHeld)
    {
        if (now - lastClick_ >= (ULONGLONG)delay)
        {
            sendRightUp();
            sendRightDown();
            lastClick_ = now;
        }
    }
}

void AutoClicker::drawSettings()
{
    widgets::CSlider("CPS##ac", &cps_, 1.0f, 30.0f, "%.0f");
    widgets::Checkbox("Left click##ac", &left_);
    widgets::Checkbox("Right click##ac", &right_);
}

MC_REGISTER_MODULE(AutoClicker);

}