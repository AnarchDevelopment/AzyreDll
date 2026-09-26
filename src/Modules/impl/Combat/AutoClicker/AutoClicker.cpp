#include "AutoClicker.hpp"
#include "GUI/Menu.hpp"
#include "GUI/Widgets.hpp"
#include "Input/SendClick.hpp"
#include "SDK/Classes/MoveInputHandler.hpp"
#include "SDK/Game.hpp"

#include <imgui.h>
#include <windows.h>

namespace mc {

namespace {

uint32_t g_rng = 0x9E3779B9u;

uint32_t randNext()
{
    uint32_t x = g_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng = x;
    return x;
}

}

AutoClicker::AutoClicker()
    : Module("AutoClicker", "Repeats the attack click at the configured CPS while you hold it.", Category::Combat, 0)
{
    markHasSettings();
}

void AutoClicker::onEnable()
{
    g_rng = (uint32_t)GetTickCount() ^ 0x9E3779B9u;
    nextClick_ = 0;
    injected_ = false;
}

void AutoClicker::onDisable()
{
    if (injected_)
    {
        sendLeftUp();
        injected_ = false;
    }
}

void AutoClicker::onShutdown()
{
    onDisable();
}

void AutoClicker::onFrame(float)
{
    if (menu::visible() || ImGui::GetIO().WantTextInput)
    {
        if (injected_)
        {
            sendLeftUp();
            injected_ = false;
        }
        return;
    }

    bool held = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    if (!held)
    {
        if (injected_)
        {
            sendLeftUp();
            injected_ = false;
        }
        nextClick_ = 0;
        return;
    }

    ULONGLONG now = GetTickCount64();
    if (nextClick_ != 0 && now < nextClick_)
        return;

    LocalPlayer local = Game::get().localPlayer();

    bool wantNative = method_ == 0 || method_ == 1;
    bool wantPhysical = method_ == 0 || method_ == 2;

    if (wantNative && local.valid())
    {
        MoveInputHandler input(local.moveInputAddr());
        if (input.valid())
        {
            input.clickFast();
            input.click();
        }
    }

    if (wantPhysical)
    {
        sendLeftUp();
        sendLeftDown();
        injected_ = true;
    }

    ++clicks_;

    float interval = 1000.0f / (float)cps_;
    float r = (float)(randNext() % 1000) * 0.001f;
    float factor = 1.0f + (r - 0.5f) * 2.0f * ((float)jitter_ * 0.01f);
    if (factor < 0.2f)
        factor = 0.2f;
    nextClick_ = now + (ULONGLONG)(interval * factor);
}

void AutoClicker::drawSettings()
{
    const char* methods[] = {"Auto (all)", "Native (InputHandler)", "Physical (SendInput)"};
    widgets::CCombo("Method##ac", &method_, methods, 3);
    widgets::CSlider("CPS##ac", &cps_, 4, 20, "%d");
    widgets::CSlider("Jitter##ac", &jitter_, 0, 50, "%d%%");

    bool inputOk = false;
    if (Game::get().localFound())
    {
        LocalPlayer local = Game::get().localPlayer();
        if (local.valid())
            inputOk = MoveInputHandler(local.moveInputAddr()).valid();
    }
    ImGui::TextDisabled("Clicks: %u   Input: %s", clicks_, inputOk ? "valid" : "invalid");
}

MC_REGISTER_MODULE(AutoClicker);

}
