#include "InputSystem.hpp"

#include "Framework/Log.hpp"
#include "GUI/Menu.hpp"
#include "Input/WheelHook.hpp"
#include "SDK/Core/Game.hpp"
#include "SDK/UI/Hooks/KeyGateHook.hpp"

#ifdef _MSC_VER
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
#endif

namespace mc::input {

#ifdef _MSC_VER
static CoreWindow g_coreWindow = nullptr;
#endif
static std::atomic<bool> g_ready{false};
// El cliente esta en primer plano (evento Activated de CoreWindow). Es la unica
// senal de foco fiable en UWP: GetForegroundWindow puede devolver el HWND del
// host y WindowFromPoint deja de resolver cuando no hay mundo cargado.
static std::atomic<bool> g_windowActive{true};
// Ultima posicion del cursor resuelta DENTRO de la ventana del juego. Se usa
// como fallback cuando WindowFromPoint no resuelve (menu principal, pantallas
// nativas): sin posicion no hay ventana hovered y el scroll se pierde.
static ImVec2 g_lastValidMouse(-FLT_MAX, -FLT_MAX);
// Escala cliente -> DisplaySize del ultimo calculo de posicion. Los deltas del
// raw input llegan en pixeles del cliente y hay que llevarlos a la escala de la UI.
static float g_uiScaleX = 1.0f;
static float g_uiScaleY = 1.0f;
// Cursor VIRTUAL de la UI. Con el puntero del CoreWindow oculto (gameplay)
// Windows congela GetCursorPos: el juego mueve la camara con los deltas del raw
// y la posicion absoluta deja de cambiar. ImGui necesita una posicion propia, asi
// que mientras la UI captura se mueve este cursor con esos mismos deltas. Si el
// puntero real SI se mueve (menu principal, pantallas nativas) se re-ancla a el.
static ImVec2 g_virtualCursor(-FLT_MAX, -FLT_MAX);
static bool g_virtualActive = false;
static POINT g_prevPhys{0, 0};
// HWND real del CoreWindow del juego, resuelto por clase + PID. En UWP
// WindowFromPoint puede devolver el HWND del host (ApplicationFrameHost) o
// nada, y entonces el cursor deja de resolver fuera de mundo (menu principal,
// pantallas nativas) y con el se pierde el scroll. Con este HWND la posicion se
// resuelve SIEMPRE que el cursor este dentro de la ventana del cliente.
static HWND g_gameWindow = nullptr;

static HWND ResolveGameWindow()
{
    if (g_gameWindow && IsWindow(g_gameWindow))
        return g_gameWindow;

    g_gameWindow = nullptr;
    const DWORD pid = GetCurrentProcessId();

    HWND hwnd = nullptr;
    while ((hwnd = FindWindowExW(nullptr, hwnd, L"Windows.UI.Core.CoreWindow", nullptr)) != nullptr)
    {
        DWORD wpid = 0;
        GetWindowThreadProcessId(hwnd, &wpid);
        if (wpid == pid)
        {
            g_gameWindow = hwnd;
            return g_gameWindow;
        }
    }

    HWND fg = GetForegroundWindow();
    if (fg)
    {
        DWORD wpid = 0;
        GetWindowThreadProcessId(fg, &wpid);
        if (wpid == pid)
            g_gameWindow = fg;
    }
    return g_gameWindow;
}
#ifdef _MSC_VER
static std::atomic<int> g_keyEvents{0};
static std::atomic<int> g_pointerEvents{0};
static std::atomic<int> g_wheelEvents{0};
static std::atomic<int> g_charEvents{0};
#endif
// Cursor del JUEGO. En gameplay el cliente oculta su puntero y el mouse mueve
// la camara; en las pantallas nativas (inventario, pausa, chat) muestra el
// suyo. Nuestra UI NUNCA escribe PointerCursor: en su lugar dibuja el cursor
// software de ImGui cuando el real esta oculto.
// Ultimo estado del OR de captura de UI. Vive fuera de updateFrame para que
// refreshUiCapture() lo deje sincronizado con el flanco automatico.
static bool lastUiOwns = false;

#ifdef _MSC_VER
struct WinRTTokens
{
    event_token keyDown{};
    event_token keyUp{};
    event_token pointerPressed{};
    event_token pointerReleased{};
    event_token pointerMoved{};
    event_token pointerWheel{};
    event_token pointerWheelDirect{};
    event_token charReceived{};
    event_token visibilityChanged{};
    event_token activated{};
    event_token sizeChanged{};
    bool valid = false;
};
static WinRTTokens g_tokens{};
#endif

// El juego decide cuando su puntero es visible: en gameplay lo oculta (el mouse
// mueve la camara) y en las pantallas nativas (inventario, pausa, chat) pone un
// Arrow en el CoreWindow. NUNCA escribimos PointerCursor: pisarlo era lo que
// dejaba al juego sin cursor y sin scroll y lo que mostraba el puntero en
// gameplay. Nuestra UI usa el cursor SOFTWARE de ImGui solo cuando el real esta
// oculto; si el juego ya muestra el suyo, se reutiliza ese y no se dibuja nada.
static bool RealCursorVisible()
{
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci))
        return false;
    return (ci.flags & CURSOR_SHOWING) != 0;
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

#ifdef _MSC_VER
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
#endif

void shutdown()
{
    g_ready = false;

#ifdef _MSC_VER
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
        g_coreWindow.PointerWheelChanged(g_tokens.pointerWheelDirect);
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

    g_coreWindow = nullptr;
#endif
}

void init()
{
    MC_LOG("[WinRT] ========================================");
    MC_LOG("[WinRT] Initializing WinRT Input System");
    MC_LOG("[WinRT] ========================================");

#ifdef _MSC_VER
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

        // Suscripcion DIRECTA del wheel (misma via que KeyDown/PointerPressed,
        // que si enganchan). Es el path del juego ACTIVO: el WH_MOUSE_LL no ve
        // el wheel cuando la ventana esta en primer plano.
        g_tokens.pointerWheelDirect = g_coreWindow.PointerWheelChanged(
            [&](CoreWindow const&, PointerEventArgs const& args)
            {
                g_wheelEvents++;
                int delta = args.CurrentPoint().Properties().MouseWheelDelta();
                if (delta != 0)
                    wheel::addWinrtDirectDelta(delta);
            });
        MC_LOG("[Wheel] PointerWheelChanged suscrito directo (init)");

        g_tokens.charReceived = g_coreWindow.CharacterReceived([&](CoreWindow const&, CharacterReceivedEventArgs const&)
                                                                { g_charEvents++; });

        g_tokens.visibilityChanged = g_coreWindow.VisibilityChanged([&](CoreWindow const&, VisibilityChangedEventArgs const& args)
                                                                    {
                                                                        MC_LOG("[WinRT] VisibilityChanged: %s",
                                                                               args.Visible() ? "Visible" : "Hidden");
                                                                    });

        g_tokens.activated = g_coreWindow.Activated([&](CoreWindow const&, WindowActivatedEventArgs const& args)
                                                     {
                                                         const bool active =
                                                             args.WindowActivationState() != CoreWindowActivationState::Deactivated;
                                                         g_windowActive = active;
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
#else
    // Toolchain no-MSVC (CI): la captura real de input va por polling en
    // updateFrame(); la suscripcion WinRT no existe en este build.
    MC_LOG("[WinRT] WinRT input unavailable in this toolchain (CI build)");
    g_ready = false;
#endif
}

// El puntero del sistema queda CONGELADO donde el CoreWindow lo oculto (el
// juego lo esconde en gameplay y Windows deja de rastrear GetCursorPos). Al
// abrir una pantalla nativa el juego vuelve a pedir su cursor, pero Windows lo
// revive en esa posicion vieja: si quedo fuera del area cliente el cursor no
// aparece dentro del juego y la rueda tampoco le llega. Recentrarlo una sola
// vez en el flanco oculto->visible lo devuelve al cliente.
static void resyncGamePointer()
{
    HWND game = ResolveGameWindow();
    if (!game || !IsWindowVisible(game))
        return;

    RECT rc{};
    if (!GetClientRect(game, &rc) || rc.right <= 0 || rc.bottom <= 0)
        return;

    POINT cur{};
    if (GetCursorPos(&cur))
    {
        POINT local = cur;
        ScreenToClient(game, &local);
        if (PtInRect(&rc, local))
            return; // ya esta dentro del cliente: no tocar nada
    }

    POINT center{(rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2};
    ClientToScreen(game, &center);
    if (SetCursorPos(center.x, center.y))
        MC_LOG("[Input] puntero del juego recentrado en (%ld,%ld)", center.x, center.y);
}

void updateFrame()
{
    ImGuiIO& io = ImGui::GetIO();

    // Suscripcion del wheel via WinRT, despachada al UI thread con el
    // Dispatcher (el patron de window::setTitle). Desde este hilo de render
    // la suscripcion directa nunca enganchaba.
#ifdef _MSC_VER
    static bool s_wheelDispatched = false;
    if (!s_wheelDispatched)
    {
        try
        {
            CoreWindow w = CoreApplication::MainView().CoreWindow();
            if (w && w.Dispatcher())
            {
                auto action = w.Dispatcher().RunAsync(
                    CoreDispatcherPriority::Normal,
                    []()
                    {
                        try
                        {
                            CoreWindow cw = CoreWindow::GetForCurrentThread();
                            if (!cw)
                                return;
                            g_tokens.pointerWheel = cw.PointerWheelChanged(
                                [&](CoreWindow const&, PointerEventArgs const& args)
                                {
                                    g_wheelEvents++;
                                    int delta = args.CurrentPoint().Properties().MouseWheelDelta();
                                    if (delta != 0)
                                        wheel::addWinrtDelta(delta);
                                });
                            MC_LOG("[Wheel] WinRT PointerWheelChanged suscrito en UI thread");
                            // NOTA: el juego lee el wheel via
                            // MouseDevice::MouseWheelChanged (Windows.Devices.
                            // Input, contrato que ya no esta en el cppwinrt del
                            // kit 26100). La captura real del juego va por el
                            // hook a MouseDevice::feed (Input/MouseHook).
                        }
                        catch (...)
                        {
                            MC_LOG("[Wheel] Excepcion suscribiendo PointerWheelChanged/MouseDevice");
                        }
                    });
                (void)action;
                s_wheelDispatched = true;
            }
        }
        catch (...)
        {
        }
    }
#endif

    bool uiOwns = mc::menu::uiOwnsInput();
    // Puntero del propio juego (gameplay = oculto, pantallas nativas = visible).
    const bool realVisible = RealCursorVisible();
    static bool lastRealVisible = false;
    if (realVisible != lastRealVisible)
    {
        lastRealVisible = realVisible;
        MC_LOG("[Input] juego cursor visible=%d", realVisible ? 1 : 0);
        // El juego acaba de mostrar su cursor (pantalla nativa). Si el puntero
        // del sistema quedo fuera del cliente, se recentra una vez.
        if (realVisible && !uiOwns)
            resyncGamePointer();
    }

    // Diagnostico del input del CLIENTE: el estado del CoreWindow decide si el
    // juego puede mostrar cursor y recibir la rueda. Un renglon por segundo.
    {
        static ULONGLONG lastDiagMs = 0;
        const ULONGLONG nowDiag = GetTickCount64();
        if (nowDiag - lastDiagMs >= 1000ull)
        {
            lastDiagMs = nowDiag;
            int inEnabled = -1;
            int ptrCursor = -1;
#ifdef _MSC_VER
            try
            {
                if (g_coreWindow)
                {
                    inEnabled = g_coreWindow.IsInputEnabled() ? 1 : 0;
                    ptrCursor = g_coreWindow.PointerCursor() ? 1 : 0;
                }
            }
            catch (...)
            {
            }
#endif
            HWND game = ResolveGameWindow();
            HWND fg = GetForegroundWindow();
            MC_LOG("[InDiag] en=%d ptr=%d fg=%d win=%d real=%d uiOwns=%d mouse=(%.0f,%.0f) evP=%llu evW=%llu evK=%llu",
                   inEnabled, ptrCursor, (game && fg == game) ? 1 : 0, game ? 1 : 0,
                   realVisible ? 1 : 0, uiOwns ? 1 : 0, io.MousePos.x, io.MousePos.y,
                   (unsigned long long)g_pointerEvents.load(),
                   (unsigned long long)g_wheelEvents.load(),
                   (unsigned long long)g_keyEvents.load());

            // Auto-reparacion: si nadie de nuestro lado captura y el CoreWindow
            // quedo con el input deshabilitado (modo UI roto), el juego se
            // queda sin mouse, sin cursor y sin rueda. Se reactiva.
#ifdef _MSC_VER
            if (!uiOwns && inEnabled == 0)
            {
                try
                {
                    g_coreWindow.IsInputEnabled(true);
                    MC_LOG("[Input] CoreWindow input re-enabled");
                }
                catch (...)
                {
                }
            }
#endif
        }
    }
    if (uiOwns != lastUiOwns)
    {
        lastUiOwns = uiOwns;
        // Telemetria del dueno de la captura: con uiOwns=1 el raw input del
        // juego queda neutralizado y la UI recibe sus propios eventos. Si esto
        // aparece en 1 sin ninguna UI abierta, hay una captura huerfana.
        // real = puntero del juego visible en ese momento.
        MC_LOG("[Input] uiOwns=%d real=%d (menu=%d hud=%d panel=%d)", uiOwns ? 1 : 0,
               realVisible ? 1 : 0, mc::menu::visible() ? 1 : 0,
               mc::menu::hudEditing() ? 1 : 0, mc::menu::uiCapture() ? 1 : 0);
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
                    g_uiScaleX = sx;
                    g_uiScaleY = sy;
                }
                posValid = true;
            }
        }
    }
    // Segunda pasada: si WindowFromPoint no resolvio al proceso se prueba con el
    // HWND del propio CoreWindow. Fuera de mundo el host de UWP responde por el
    // cursor y sin esto no hay hover ni scroll aunque el cliente este en foco.
    if (!posValid)
    {
        HWND game = ResolveGameWindow();
        if (game && IsWindowVisible(game))
        {
            POINT cp{};
            if (GetCursorPos(&cp) && ScreenToClient(game, &cp))
            {
                RECT rc{};
                if (GetClientRect(game, &rc) && rc.right > 0 && rc.bottom > 0 &&
                    cp.x >= 0 && cp.y >= 0 && cp.x < rc.right && cp.y < rc.bottom)
                {
                    float sx = io.DisplaySize.x > 0.0f ? io.DisplaySize.x / (float)rc.right : 1.0f;
                    float sy = io.DisplaySize.y > 0.0f ? io.DisplaySize.y / (float)rc.bottom : 1.0f;
                    p.x = (LONG)(cp.x * sx);
                    p.y = (LONG)(cp.y * sy);
                    g_uiScaleX = sx;
                    g_uiScaleY = sy;
                    posValid = true;
                }
            }
        }
    }

    // Deltas del mouse de ESTE frame: la unica posicion fiable cuando el puntero
    // del CoreWindow esta oculto (GetCursorPos congelada).
    int mdx = 0, mdy = 0;
    wheel::consumeMouseDelta(mdx, mdy);

    if (uiOwns)
    {
        const bool realMoved = posValid && (p.x != g_prevPhys.x || p.y != g_prevPhys.y);
        if (!g_virtualActive || realMoved)
        {
            // Ancla: posicion real si resolvio; si no, la ultima buena; si no,
            // el centro de la pantalla (el panel siempre aparece con puntero).
            if (posValid)
                g_virtualCursor = ImVec2((float)p.x, (float)p.y);
            else if (g_lastValidMouse.x > -FLT_MAX)
                g_virtualCursor = g_lastValidMouse;
            else
                g_virtualCursor = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
            g_virtualActive = true;
            mdx = 0;
            mdy = 0;
        }
        g_virtualCursor.x += (float)mdx * g_uiScaleX;
        g_virtualCursor.y += (float)mdy * g_uiScaleY;
        const float maxX = io.DisplaySize.x > 1.0f ? io.DisplaySize.x - 1.0f : 0.0f;
        const float maxY = io.DisplaySize.y > 1.0f ? io.DisplaySize.y - 1.0f : 0.0f;
        g_virtualCursor.x = g_virtualCursor.x < 0.0f ? 0.0f : (g_virtualCursor.x > maxX ? maxX : g_virtualCursor.x);
        g_virtualCursor.y = g_virtualCursor.y < 0.0f ? 0.0f : (g_virtualCursor.y > maxY ? maxY : g_virtualCursor.y);
        io.MousePos = g_virtualCursor;
    }
    else
    {
        g_virtualActive = false;
        if (posValid)
            io.MousePos = ImVec2((float)p.x, (float)p.y);
        else if (g_windowActive.load() && g_lastValidMouse.x > -FLT_MAX)
        {
            // Fuera de mundo (menu principal, pantallas del juego) WindowFromPoint
            // no resuelve al proceso: sin posicion valida ImGui no tiene ventana
            // hovered y la rueda se pierde. Se conserva la ultima posicion buena
            // mientras el cliente siga en primer plano.
            io.MousePos = g_lastValidMouse;
        }
        else
            io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
    }

    if (posValid)
    {
        g_lastValidMouse = ImVec2((float)p.x, (float)p.y);
        g_prevPhys = p;
    }

    io.MouseDown[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    io.MouseDown[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    io.MouseDown[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
    io.MouseDown[3] = (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0;
    io.MouseDown[4] = (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0;

    // ImGui >= 1.87: la escritura directa de io.MouseWheel es obsoleta
    // (EndFrame la zeroea y solo los EVENTOS la repueblan en NewFrame).
    // Hay que inyectar via AddMouseWheelEvent o el scroll de todas las
    // ventanas queda muerto.
    //
    // Gate del wheel: se entrega si el cursor esta sobre la ventana del juego o
    // si el cliente esta en primer plano (aunque la posicion no haya resuelto).
    // Antes solo valia posValid, asi que la rueda moria en el menu principal y
    // en las pantallas del juego. Fuera de mundo basta el foco: si no hay
    // ventana hovered ImGui descarta el evento por si solo, y si la hay (menu
    // del cliente) el scroll funciona igual que dentro del mundo.
    const bool wheelTarget = posValid || g_windowActive.load();
    float wheelDelta = wheel::consume();
    if (wheelDelta != 0.0f)
    {
        // [WheelDBG][Input]: estado del routing en el momento del evento.
        // posValid=0 con focus=1 = cursor no resuelto pero cliente en primer
        // plano (menu principal / pantalla del juego) o cursor fuera de la
        // ventana: el evento se entrega igual y el hover decide.
        static ULONGLONG lastInDbg = 0;
        ULONGLONG nowInDbg = GetTickCount64();
        if (nowInDbg - lastInDbg > 400ull)
        {
            lastInDbg = nowInDbg;
            MC_LOG("[WheelDBG][Input] consume=%.2f posValid=%d focus=%d mouse=(%.0f,%.0f)",
                   wheelDelta, posValid ? 1 : 0, g_windowActive.load() ? 1 : 0,
                   io.MousePos.x, io.MousePos.y);
        }
        if (wheelTarget)
        {
            io.AddMouseWheelEvent(0.0f, wheelDelta);
            // Diagnostico del pipeline (ver tambien [WheelDBG] tras NewFrame).
            static ULONGLONG lastWheelDbg = 0;
            ULONGLONG nowDbg = GetTickCount64();
            if (nowDbg - lastWheelDbg > 200ull)
            {
                lastWheelDbg = nowDbg;
                MC_LOG("[WheelDBG] consume=%.2f -> evento encolado", wheelDelta);
            }
        }
    }

    // En UWP GetCursorInfo puede quedarse en CURSOR_SHOWING=0 incluso cuando
    // el juego ha habilitado su CoreCursor para inventario/pausa. Detectamos
    // ese estado por PointerCursor, sin escribir la propiedad, y dibujamos el
    // cursor software cuando Windows no presenta uno real.
    bool corePointerAvailable = false;
#ifdef _MSC_VER
    try
    {
        corePointerAvailable = g_coreWindow && (g_coreWindow.PointerCursor() ? true : false);
    }
    catch (...)
    {
    }
#endif
    io.MouseDrawCursor = !realVisible && (uiOwns || corePointerAvailable);

    io.KeyCtrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    io.KeyShift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    io.KeyAlt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    io.KeySuper = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;

    static bool keyWasDown[256] = {};
    const bool wantChars = uiOwns || io.WantTextInput ||
                           keygate::shouldCaptureKeyboard();
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

void refreshUiCapture()
{
    const bool owns = mc::menu::uiOwnsInput();
    lastUiOwns = owns;
}

bool windowActive()
{
    return g_windowActive.load(std::memory_order_relaxed);
}

}
