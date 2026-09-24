#include "InputSystem.hpp"

#include "Framework/Log.hpp"
#include "GUI/Menu.hpp"
#include "Input/WheelHook.hpp"
#include "SDK/Game.hpp"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.UI.ViewManagement.h>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::UI::Core;
using namespace Windows::System;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::UI::ViewManagement;

namespace mc::input {

static CoreWindow g_coreWindow = nullptr;
static std::atomic<bool> g_ready{false};
static std::atomic<int> g_keyEvents{0};
static std::atomic<int> g_pointerEvents{0};
static std::atomic<int> g_wheelEvents{0};
static std::atomic<int> g_charEvents{0};
static bool g_cursorForcedVisible = false;

struct WinRTTokens
{
    event_token keyDown{};
    event_token keyUp{};
    event_token pointerPressed{};
    event_token pointerReleased{};
    event_token pointerMoved{};
    event_token pointerWheel{};
    event_token charReceived{};
    event_token visibilityChanged{};
    event_token activated{};
    event_token sizeChanged{};
    bool valid = false;
};
static WinRTTokens g_tokens{};

static bool IsSystemCursorShowing()
{
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (GetCursorInfo(&ci))
        return (ci.flags & CURSOR_SHOWING) != 0;
    return false;
}

static void ApplyCursorForMenu(bool menuOpen)
{
    try
    {
        CoreWindow win = g_coreWindow;
        if (!win)
            win = CoreWindow::GetForCurrentThread();
        if (!win)
        {
            g_cursorForcedVisible = false;
            return;
        }

        if (menuOpen)
        {
            bool inWorld = !IsSystemCursorShowing();
            if (inWorld)
            {
                CoreCursor arrowCursor{CoreCursorType::Arrow, 0};
                win.PointerCursor(arrowCursor);
                g_cursorForcedVisible = true;
                MC_LOG("[WinRT] Menu open in world: system cursor shown");
            }
        }
        else if (g_cursorForcedVisible)
        {
            if (mc::Game::get().localFound())
            {
                win.PointerCursor(nullptr);
                MC_LOG("[WinRT] Menu closed: system cursor hidden");
            }
            g_cursorForcedVisible = false;
        }
    }
    catch (...)
    {
        g_cursorForcedVisible = false;
    }
}

static ImGuiKey MapVkToImGuiKey(int vk)
{
    if (vk >= 'A' && vk <= 'Z')
        return (ImGuiKey)(ImGuiKey_A + (vk - 'A'));
    if (vk >= '0' && vk <= '9')
        return (ImGuiKey)(ImGuiKey_0 + (vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F12)
        return (ImGuiKey)(ImGuiKey_F1 + (vk - VK_F1));
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        return (ImGuiKey)(ImGuiKey_Keypad0 + (vk - VK_NUMPAD0));

    switch (vk)
    {
    case VK_DECIMAL: return ImGuiKey_KeypadDecimal;
    case VK_DIVIDE: return ImGuiKey_KeypadDivide;
    case VK_MULTIPLY: return ImGuiKey_KeypadMultiply;
    case VK_SUBTRACT: return ImGuiKey_KeypadSubtract;
    case VK_ADD: return ImGuiKey_KeypadAdd;
    case VK_TAB: return ImGuiKey_Tab;
    case VK_LEFT: return ImGuiKey_LeftArrow;
    case VK_RIGHT: return ImGuiKey_RightArrow;
    case VK_UP: return ImGuiKey_UpArrow;
    case VK_DOWN: return ImGuiKey_DownArrow;
    case VK_PRIOR: return ImGuiKey_PageUp;
    case VK_NEXT: return ImGuiKey_PageDown;
    case VK_HOME: return ImGuiKey_Home;
    case VK_END: return ImGuiKey_End;
    case VK_INSERT: return ImGuiKey_Insert;
    case VK_DELETE: return ImGuiKey_Delete;
    case VK_BACK: return ImGuiKey_Backspace;
    case VK_SPACE: return ImGuiKey_Space;
    case VK_RETURN: return ImGuiKey_Enter;
    case VK_ESCAPE: return ImGuiKey_Escape;
    case VK_LCONTROL: return ImGuiKey_LeftCtrl;
    case VK_RCONTROL: return ImGuiKey_RightCtrl;
    case VK_CONTROL: return ImGuiKey_LeftCtrl;
    case VK_LSHIFT: return ImGuiKey_LeftShift;
    case VK_RSHIFT: return ImGuiKey_RightShift;
    case VK_SHIFT: return ImGuiKey_LeftShift;
    case VK_LMENU: return ImGuiKey_LeftAlt;
    case VK_RMENU: return ImGuiKey_RightAlt;
    case VK_MENU: return ImGuiKey_LeftAlt;
    case VK_LWIN: return ImGuiKey_LeftSuper;
    case VK_RWIN: return ImGuiKey_RightSuper;
    case VK_APPS: return ImGuiKey_Menu;
    case VK_CAPITAL: return ImGuiKey_CapsLock;
    case VK_SCROLL: return ImGuiKey_ScrollLock;
    case VK_NUMLOCK: return ImGuiKey_NumLock;
    case VK_SNAPSHOT: return ImGuiKey_PrintScreen;
    case VK_PAUSE: return ImGuiKey_Pause;
    case VK_OEM_6: return ImGuiKey_RightBracket;
    case VK_OEM_5: return ImGuiKey_Backslash;
    case VK_OEM_4: return ImGuiKey_LeftBracket;
    case VK_OEM_3: return ImGuiKey_GraveAccent;
    case VK_OEM_1: return ImGuiKey_Semicolon;
    case VK_OEM_PLUS: return ImGuiKey_Equal;
    case VK_OEM_COMMA: return ImGuiKey_Comma;
    case VK_OEM_PERIOD: return ImGuiKey_Period;
    case VK_OEM_2: return ImGuiKey_Slash;
    case VK_OEM_7: return ImGuiKey_Apostrophe;
    default: return ImGuiKey_None;
    }
}

static void BuildKeyboardState(BYTE keyboardState[256])
{
    memset(keyboardState, 0, 256);
    for (int i = 0; i < 256; i++)
    {
        SHORT s = GetAsyncKeyState(i);
        if (s & 0x8000)
            keyboardState[i] |= 0x80;
        if (s & 0x0001)
            keyboardState[i] |= 0x01;
    }
}

static HKL GetActiveKeyboardLayout()
{
    DWORD threadId = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
    if (threadId)
    {
        HKL hkl = GetKeyboardLayout(threadId);
        if (hkl)
            return hkl;
    }
    return GetKeyboardLayout(0);
}

static unsigned short VKToAsciiFallback(int vk, const BYTE keyboardState[256])
{
    const bool shift = (keyboardState[VK_SHIFT] & 0x80) != 0;
    const bool caps = (keyboardState[VK_CAPITAL] & 0x01) != 0;
    const bool upper = shift ^ caps;

    if (vk >= 'A' && vk <= 'Z')
        return (unsigned short)(upper ? vk : vk + 32);
    if (vk >= '0' && vk <= '9')
        return (unsigned short)(shift ? ")!@#$%^&*("[vk - '0'] : vk);
    if (vk == VK_SPACE)
        return ' ';
    if (vk == VK_OEM_1) return shift ? ':' : ';';
    if (vk == VK_OEM_PLUS) return shift ? '+' : '=';
    if (vk == VK_OEM_COMMA) return shift ? '<' : ',';
    if (vk == VK_OEM_MINUS) return shift ? '_' : '-';
    if (vk == VK_OEM_PERIOD) return shift ? '>' : '.';
    if (vk == VK_OEM_2) return shift ? '?' : '/';
    if (vk == VK_OEM_3) return shift ? '~' : '`';
    if (vk == VK_OEM_4) return shift ? '{' : '[';
    if (vk == VK_OEM_5) return shift ? '|' : '\\';
    if (vk == VK_OEM_6) return shift ? '}' : ']';
    if (vk == VK_OEM_7) return shift ? '"' : '\'';
    if (vk == VK_OEM_102) return shift ? '|' : '\\';
    return 0;
}

static CoreWindow GetCoreWindowInfo(float& width, float& height)
{
    MC_LOG("[WinRT] Attempting to get CoreWindow...");

    try
    {
        auto view = CoreApplication::GetCurrentView();
        if (view)
        {
            auto window = view.CoreWindow();
            if (window)
            {
                auto bounds = window.Bounds();
                width = (float)bounds.Width;
                height = (float)bounds.Height;
                MC_LOG("[WinRT] CoreWindow size: %.0f x %.0f", width, height);
                return window;
            }
        }

        auto window = CoreWindow::GetForCurrentThread();
        if (window)
        {
            auto bounds = window.Bounds();
            width = (float)bounds.Width;
            height = (float)bounds.Height;
            MC_LOG("[WinRT] CoreWindow size (thread): %.0f x %.0f", width, height);
            return window;
        }

        MC_LOG("[WinRT] No CoreWindow found");
        width = 0;
        height = 0;
        return nullptr;
    }
    catch (const winrt::hresult_error& e)
    {
        MC_LOG("[WinRT] ERROR: %s", winrt::to_string(e.message()).c_str());
        width = 0;
        height = 0;
        return nullptr;
    }
    catch (...)
    {
        MC_LOG("[WinRT] Unknown error getting CoreWindow");
        width = 0;
        height = 0;
        return nullptr;
    }
}

void shutdown()
{
    g_ready = false;

    if (!g_coreWindow || !g_tokens.valid)
        return;

    try
    {
        g_coreWindow.KeyDown(g_tokens.keyDown);
        g_coreWindow.KeyUp(g_tokens.keyUp);
        g_coreWindow.PointerPressed(g_tokens.pointerPressed);
        g_coreWindow.PointerReleased(g_tokens.pointerReleased);
        g_coreWindow.PointerMoved(g_tokens.pointerMoved);
        g_coreWindow.PointerWheelChanged(g_tokens.pointerWheel);
        g_coreWindow.CharacterReceived(g_tokens.charReceived);
        g_coreWindow.VisibilityChanged(g_tokens.visibilityChanged);
        g_coreWindow.Activated(g_tokens.activated);
        g_coreWindow.SizeChanged(g_tokens.sizeChanged);
        g_tokens.valid = false;
        MC_LOG("[WinRT] Event handlers revoked");
    }
    catch (...)
    {
    }

    if (g_cursorForcedVisible)
    {
        try
        {
            CoreWindow win = g_coreWindow;
            if (win)
                win.PointerCursor(nullptr);
        }
        catch (...)
        {
        }
        g_cursorForcedVisible = false;
    }

    g_coreWindow = nullptr;
}

void init()
{
    MC_LOG("[WinRT] ========================================");
    MC_LOG("[WinRT] Initializing WinRT Input System");
    MC_LOG("[WinRT] ========================================");

    try
    {
        float w = 0.0f, h = 0.0f;
        g_coreWindow = GetCoreWindowInfo(w, h);
        if (!g_coreWindow)
        {
            MC_LOG("[WinRT] Failed to get CoreWindow - WinRT input unavailable");
            return;
        }

        g_ready = true;

        g_tokens.keyDown = g_coreWindow.KeyDown([&](CoreWindow const&, KeyEventArgs const&)
                                                { g_keyEvents++; });

        g_tokens.keyUp = g_coreWindow.KeyUp([&](CoreWindow const&, KeyEventArgs const&)
                                            { g_keyEvents++; });

        g_tokens.pointerPressed = g_coreWindow.PointerPressed([&](CoreWindow const&, PointerEventArgs const&)
                                                               { g_pointerEvents++; });

        g_tokens.pointerReleased = g_coreWindow.PointerReleased([&](CoreWindow const&, PointerEventArgs const&)
                                                                 { g_pointerEvents++; });

        g_tokens.pointerMoved = g_coreWindow.PointerMoved([&](CoreWindow const&, PointerEventArgs const&) {});

        g_tokens.charReceived = g_coreWindow.CharacterReceived([&](CoreWindow const&, CharacterReceivedEventArgs const&)
                                                                { g_charEvents++; });

        g_tokens.visibilityChanged = g_coreWindow.VisibilityChanged([&](CoreWindow const&, VisibilityChangedEventArgs const& args)
                                                                    {
                                                                        MC_LOG("[WinRT] VisibilityChanged: %s",
                                                                               args.Visible() ? "Visible" : "Hidden");
                                                                    });

        g_tokens.activated = g_coreWindow.Activated([&](CoreWindow const&, WindowActivatedEventArgs const& args)
                                                     {
                                                         MC_LOG("[WinRT] WindowActivated: %s",
                                                                args.WindowActivationState() != CoreWindowActivationState::Deactivated ? "Active" : "Inactive");
                                                     });

        g_tokens.sizeChanged = g_coreWindow.SizeChanged([&](CoreWindow const&, WindowSizeChangedEventArgs const& args)
                                                         {
                                                             auto size = args.Size();
                                                             MC_LOG("[WinRT] WindowSizeChanged: %.0f x %.0f",
                                                                    (float)size.Width, (float)size.Height);
                                                         });

        g_tokens.valid = true;
        MC_LOG("[WinRT] WinRT input initialized successfully");
    }
    catch (const winrt::hresult_error& e)
    {
        MC_LOG("[WinRT] ERROR initializing: %s", winrt::to_string(e.message()).c_str());
        g_ready = false;
    }
    catch (...)
    {
        MC_LOG("[WinRT] Unknown error initializing WinRT");
        g_ready = false;
    }
}

void updateFrame()
{
    ImGuiIO& io = ImGui::GetIO();

    static bool s_wheelSubscribed = false;
    if (!s_wheelSubscribed)
    {
        try
        {
            CoreWindow w = CoreWindow::GetForCurrentThread();
            if (!w)
            {
                auto appView = CoreApplication::MainView();
                if (appView)
                    w = appView.CoreWindow();
            }
            if (!w)
            {
                auto curView = CoreApplication::GetCurrentView();
                if (curView)
                    w = curView.CoreWindow();
            }
            if (w)
            {
                g_tokens.pointerWheel = w.PointerWheelChanged(
                    [&](CoreWindow const&, PointerEventArgs const& args)
                    {
                        g_wheelEvents++;
                        int delta = args.CurrentPoint().Properties().MouseWheelDelta();
                        if (delta != 0)
                            wheel::addWinrtDelta(delta);
                    });
                s_wheelSubscribed = true;
                MC_LOG("[Wheel] WinRT subscribed");
            }
        }
        catch (...)
        {
        }
    }

    static bool lastMenuVisible = false;
    bool menuOpenNow = mc::menu::visible();
    if (menuOpenNow != lastMenuVisible)
    {
        ApplyCursorForMenu(menuOpenNow);
        lastMenuVisible = menuOpenNow;
    }

    POINT p{};
    bool posValid = false;
    if (GetCursorPos(&p))
    {
        HWND under = WindowFromPoint(p);
        HWND consoleWnd = GetConsoleWindow();
        bool overConsole = consoleWnd && (under == consoleWnd || IsChild(consoleWnd, under));
        if (under && !overConsole)
        {
            DWORD pid = 0;
            GetWindowThreadProcessId(under, &pid);
            if (pid == GetCurrentProcessId())
            {
                ScreenToClient(under, &p);
                RECT rc{};
                if (GetClientRect(under, &rc) && rc.right > 0 && rc.bottom > 0 &&
                    io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f)
                {
                    float sx = io.DisplaySize.x / (float)rc.right;
                    float sy = io.DisplaySize.y / (float)rc.bottom;
                    p.x = (LONG)(p.x * sx);
                    p.y = (LONG)(p.y * sy);
                }
                posValid = true;
            }
        }
    }
    if (posValid)
        io.MousePos = ImVec2((float)p.x, (float)p.y);
    else
        io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);

    io.MouseDown[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    io.MouseDown[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    io.MouseDown[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
    io.MouseDown[3] = (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0;
    io.MouseDown[4] = (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0;

    io.MouseWheel = wheel::consume();

    io.MouseDrawCursor = menuOpenNow && !IsSystemCursorShowing();

    io.KeyCtrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    io.KeyShift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    io.KeyAlt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    io.KeySuper = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;

    static bool keyWasDown[256] = {};
    const bool wantChars = menuOpenNow || io.WantTextInput;
    BYTE keyboardState[256];
    bool ksBuilt = false;
    HKL layout = nullptr;

    for (int i = 0; i < 256; i++)
    {
        if (i >= VK_F13 && i <= VK_F24)
            continue;

        bool down = (GetAsyncKeyState(i) & 0x8000) != 0;
        bool pressed = down && !keyWasDown[i];
        bool released = !down && keyWasDown[i];
        keyWasDown[i] = down;

        ImGuiKey imKey = MapVkToImGuiKey(i);
        if (imKey != ImGuiKey_None)
        {
            if (pressed)
                io.AddKeyEvent(imKey, true);
            if (released)
                io.AddKeyEvent(imKey, false);
        }

        if (pressed && wantChars && i != VK_SHIFT && i != VK_CONTROL && i != VK_MENU && i != VK_LWIN && i != VK_RWIN)
        {
            if (!ksBuilt)
            {
                BuildKeyboardState(keyboardState);
                ksBuilt = true;
            }
            if (!layout)
                layout = GetActiveKeyboardLayout();

            UINT scan = MapVirtualKey((UINT)i, MAPVK_VK_TO_VSC);
            WCHAR buffer[4];
            int result = ToUnicodeEx((UINT)i, scan, keyboardState, buffer, 4, 0, layout);
            if (result > 0)
            {
                for (int j = 0; j < result; j++)
                    io.AddInputCharacterUTF16((unsigned short)buffer[j]);
            }
            else
            {
                unsigned short c = VKToAsciiFallback(i, keyboardState);
                if (c)
                    io.AddInputCharacterUTF16(c);
            }
        }
    }
}

}
