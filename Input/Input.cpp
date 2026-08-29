/*
Under an4rch Development Public Source License 1.0
*/

#include "Input.hpp"
#include "../Modules/Globals.hpp"
#include "../Modules/Info/Info.hpp"
#include "../Modules/Visuals/MouseStrokes/MouseStrokes.hpp"
#include "../ImGui/backend/imgui_impl_win32.h"
#include <vector>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <appmodel.h>
#include <shlobj.h>
#include <knownfolders.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Set to true once the game delivers a WM_CHAR to our subclassed WndProc. In that
// case characters come from the message queue and the polling fallback must be
// disabled to avoid double-adding them (UWP/CoreWindow games never send WM_CHAR).
static bool g_wndProcDeliversChars = false;

// Build a fresh keyboard state table from GetAsyncKeyState(). GetKeyboardState()
// returns the *calling thread's* input table, which is stale/empty on the render
// thread and makes ToUnicode() return 0.
static void BuildKeyboardState(BYTE keyboardState[256]) {
    memset(keyboardState, 0, 256);
    for (int i = 0; i < 256; i++) {
        SHORT s = GetAsyncKeyState(i);
        if (s & 0x8000)
            keyboardState[i] |= 0x80;
        if (s & 0x0001)
            keyboardState[i] |= 0x01;
    }
}

// Keyboard layout of the foreground window (correct even if the render thread's
// default layout is stale).
static HKL GetActiveKeyboardLayout() {
    DWORD threadId = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
    if (threadId) {
        HKL hkl = GetKeyboardLayout(threadId);
        if (hkl)
            return hkl;
    }
    return GetKeyboardLayout(0);
}

// -----------------------------------------------------------------------------
// Full keyboard capture via WH_KEYBOARD_LL.
//
// The game (UWP/Minecraft Bedrock) never delivers WM_KEYDOWN/WM_CHAR to our
// subclassed WndProc, so we hook the whole keyboard. A low-level hook only fires
// while its installing thread pumps messages, therefore it runs on a dedicated
// thread. The hook thread NEVER touches ImGui or calls ToUnicode: it only queues
// raw key events under a lock. UpdateKeyboard() (render thread) drains them and
// converts vk/scan -> ImGui key + character there, where ToUnicodeEx() works.
// -----------------------------------------------------------------------------

struct InputEvent {
    int vk;
    UINT scan;
    bool down;
};

static std::vector<InputEvent> g_inputEvents;
static CRITICAL_SECTION g_inputLock;
static volatile LONG g_inputLockInitialized = 0;
static volatile LONG g_keyboardHookActive = 0;
static HANDLE g_keyboardHookThread = NULL;
static DWORD g_keyboardHookThreadId = 0;

static void EnsureInputLock() {
    if (InterlockedCompareExchange(&g_inputLockInitialized, 1, 0) == 0)
        InitializeCriticalSection(&g_inputLock);
}

// Guaranteed character for a virtual key, used when ToUnicodeEx() can't resolve
// one (e.g. on a thread without a proper input context). US-layout mapping.
static unsigned short VKToAsciiFallback(int vk, const BYTE keyboardState[256]) {
    const bool shift = (keyboardState[VK_SHIFT] & 0x80) != 0;
    const bool caps = (keyboardState[VK_CAPITAL] & 0x01) != 0;
    const bool upper = shift ^ caps;

    if (vk >= 'A' && vk <= 'Z')
        return (unsigned short)(upper ? vk : vk + 32);
    if (vk >= '0' && vk <= '9')
        return (unsigned short)(shift ? ")!@#$%^&*("[vk - '0'] : vk);
    if (vk == VK_SPACE)
        return ' ';
    if (vk == VK_OEM_1)      return shift ? ':' : ';';
    if (vk == VK_OEM_PLUS)   return shift ? '+' : '=';
    if (vk == VK_OEM_COMMA)  return shift ? '<' : ',';
    if (vk == VK_OEM_MINUS)  return shift ? '_' : '-';
    if (vk == VK_OEM_PERIOD) return shift ? '>' : '.';
    if (vk == VK_OEM_2)      return shift ? '?' : '/';
    if (vk == VK_OEM_3)      return shift ? '~' : '`';
    if (vk == VK_OEM_4)      return shift ? '{' : '[';
    if (vk == VK_OEM_5)      return shift ? '|' : '\\';
    if (vk == VK_OEM_6)      return shift ? '}' : ']';
    if (vk == VK_OEM_7)      return shift ? '"' : '\'';
    if (vk == VK_OEM_102)    return shift ? '|' : '\\';
    return 0;
}

// Static member definitions

bool Input::g_keys[256] = {};
bool Input::g_keysPressed[256] = {};
bool Input::g_keysReleased[256] = {};

// Mouse button state tracking
bool Input::g_prevLmbPressed = false;
bool Input::g_prevRmbPressed = false;
bool Input::g_lmbPressed = false;
bool Input::g_rmbPressed = false;

// Virtual key to ImGui key mapping
ImGuiKey Input::VKToImGuiKey(int vk) {
    switch (vk) {
        case VK_TAB:            return ImGuiKey_Tab;
        case VK_LEFT:           return ImGuiKey_LeftArrow;
        case VK_RIGHT:          return ImGuiKey_RightArrow;
        case VK_UP:             return ImGuiKey_UpArrow;
        case VK_DOWN:           return ImGuiKey_DownArrow;
        case VK_PRIOR:          return ImGuiKey_PageUp;
        case VK_NEXT:           return ImGuiKey_PageDown;
        case VK_HOME:           return ImGuiKey_Home;
        case VK_END:            return ImGuiKey_End;
        case VK_INSERT:         return ImGuiKey_Insert;
        case VK_DELETE:         return ImGuiKey_Delete;
        case VK_BACK:           return ImGuiKey_Backspace;
        case VK_SPACE:          return ImGuiKey_Space;
        case VK_RETURN:         return ImGuiKey_Enter;
        case VK_ESCAPE:         return ImGuiKey_Escape;
        case VK_SHIFT:          return ImGuiKey_LeftShift;
        case VK_LSHIFT:         return ImGuiKey_LeftShift;
        case VK_RSHIFT:         return ImGuiKey_RightShift;
        case VK_CONTROL:        return ImGuiKey_LeftCtrl;
        case VK_LCONTROL:       return ImGuiKey_LeftCtrl;
        case VK_RCONTROL:       return ImGuiKey_RightCtrl;
        case VK_MENU:           return ImGuiKey_LeftAlt;
        case VK_LMENU:          return ImGuiKey_LeftAlt;
        case VK_RMENU:          return ImGuiKey_RightAlt;
        case VK_LWIN:           return ImGuiKey_LeftSuper;
        case VK_RWIN:           return ImGuiKey_RightSuper;
        case VK_CAPITAL:        return ImGuiKey_CapsLock;
        case VK_SCROLL:         return ImGuiKey_ScrollLock;
        case VK_NUMLOCK:        return ImGuiKey_NumLock;
        case VK_F1:             return ImGuiKey_F1;
        case VK_F2:             return ImGuiKey_F2;
        case VK_F3:             return ImGuiKey_F3;
        case VK_F4:             return ImGuiKey_F4;
        case VK_F5:             return ImGuiKey_F5;
        case VK_F6:             return ImGuiKey_F6;
        case VK_F7:             return ImGuiKey_F7;
        case VK_F8:             return ImGuiKey_F8;
        case VK_F9:             return ImGuiKey_F9;
        case VK_F10:            return ImGuiKey_F10;
        case VK_F11:            return ImGuiKey_F11;
        case VK_F12:            return ImGuiKey_F12;
        // Letters
        case 'A':               return ImGuiKey_A;
        case 'B':               return ImGuiKey_B;
        case 'C':               return ImGuiKey_C;
        case 'D':               return ImGuiKey_D;
        case 'E':               return ImGuiKey_E;
        case 'F':               return ImGuiKey_F;
        case 'G':               return ImGuiKey_G;
        case 'H':               return ImGuiKey_H;
        case 'I':               return ImGuiKey_I;
        case 'J':               return ImGuiKey_J;
        case 'K':               return ImGuiKey_K;
        case 'L':               return ImGuiKey_L;
        case 'M':               return ImGuiKey_M;
        case 'N':               return ImGuiKey_N;
        case 'O':               return ImGuiKey_O;
        case 'P':               return ImGuiKey_P;
        case 'Q':               return ImGuiKey_Q;
        case 'R':               return ImGuiKey_R;
        case 'S':               return ImGuiKey_S;
        case 'T':               return ImGuiKey_T;
        case 'U':               return ImGuiKey_U;
        case 'V':               return ImGuiKey_V;
        case 'W':               return ImGuiKey_W;
        case 'X':               return ImGuiKey_X;
        case 'Y':               return ImGuiKey_Y;
        case 'Z':               return ImGuiKey_Z;
        // Digits (row and keypad)
        case '0':               return ImGuiKey_0;
        case '1':               return ImGuiKey_1;
        case '2':               return ImGuiKey_2;
        case '3':               return ImGuiKey_3;
        case '4':               return ImGuiKey_4;
        case '5':               return ImGuiKey_5;
        case '6':               return ImGuiKey_6;
        case '7':               return ImGuiKey_7;
        case '8':               return ImGuiKey_8;
        case '9':               return ImGuiKey_9;
        case VK_NUMPAD0:        return ImGuiKey_Keypad0;
        case VK_NUMPAD1:        return ImGuiKey_Keypad1;
        case VK_NUMPAD2:        return ImGuiKey_Keypad2;
        case VK_NUMPAD3:        return ImGuiKey_Keypad3;
        case VK_NUMPAD4:        return ImGuiKey_Keypad4;
        case VK_NUMPAD5:        return ImGuiKey_Keypad5;
        case VK_NUMPAD6:        return ImGuiKey_Keypad6;
        case VK_NUMPAD7:        return ImGuiKey_Keypad7;
        case VK_NUMPAD8:        return ImGuiKey_Keypad8;
        case VK_NUMPAD9:        return ImGuiKey_Keypad9;
        case VK_DECIMAL:        return ImGuiKey_KeypadDecimal;
        case VK_DIVIDE:         return ImGuiKey_KeypadDivide;
        case VK_MULTIPLY:       return ImGuiKey_KeypadMultiply;
        case VK_SUBTRACT:       return ImGuiKey_KeypadSubtract;
        case VK_ADD:            return ImGuiKey_KeypadAdd;
        // OEM punctuation (non-US layouts handled by the character path)
        case VK_OEM_1:          return ImGuiKey_Semicolon;
        case VK_OEM_PLUS:       return ImGuiKey_Equal;
        case VK_OEM_COMMA:      return ImGuiKey_Comma;
        case VK_OEM_MINUS:      return ImGuiKey_Minus;
        case VK_OEM_PERIOD:     return ImGuiKey_Period;
        case VK_OEM_2:          return ImGuiKey_Slash;
        case VK_OEM_3:          return ImGuiKey_GraveAccent;
        case VK_OEM_4:          return ImGuiKey_LeftBracket;
        case VK_OEM_5:          return ImGuiKey_Backslash;
        case VK_OEM_6:          return ImGuiKey_RightBracket;
        case VK_OEM_7:          return ImGuiKey_Apostrophe;
        case VK_OEM_102:        return ImGuiKey_Oem102;
        default:                return ImGuiKey_None;
    }
}

void Input::UpdateKeyboard(bool menuOpen) {
    ImGuiIO& io = ImGui::GetIO();

    // Whether the game delivers a *printable* WM_CHAR through our subclassed
    // WndProc is re-evaluated every frame so the fallback sources re-enable
    // themselves if it ever stops (and vice-versa). Garbage WM_CHAR (wParam 0,
    // control codes) must NOT count -- the backend won't add those to ImGui.
    g_wndProcDeliversChars = false;

    // Keys whose down/up event the hook already fed this frame. The polling
    // pass below skips them so one physical press never produces a duplicate
    // key/character event. Unlike g_keys[], this is only a per-frame marker.
    static BYTE hookHandled[256];

    // --- (1) Reliable key capture from the WH_KEYBOARD_LL hook ---
    if (g_keyboardHookActive) {
        EnsureInputLock();

        std::vector<InputEvent> events;
        EnterCriticalSection(&g_inputLock);
        events.swap(g_inputEvents);
        LeaveCriticalSection(&g_inputLock);

        memset(hookHandled, 0, sizeof(hookHandled));

        BYTE keyboardState[256] = {};
        HKL layout = nullptr;
        const bool genChars = (menuOpen || io.WantTextInput) && !g_wndProcDeliversChars;

        for (size_t i = 0; i < events.size(); i++) {
            const InputEvent& ev = events[i];
            if (ev.vk < 0 || ev.vk > 255)
                continue;

            ImGuiKey key = VKToImGuiKey(ev.vk);
            if (key != ImGuiKey_None)
                io.AddKeyEvent(key, ev.down);

            hookHandled[ev.vk] = 1;

            if (ev.down && genChars) {
                if (!layout) {
                    BuildKeyboardState(keyboardState);
                    layout = GetActiveKeyboardLayout();
                }

                WCHAR buffer[4];
                int result = ToUnicodeEx(ev.vk, ev.scan, keyboardState, buffer, 4, 0, layout);
                if (result > 0) {
                    for (int j = 0; j < result; j++)
                        io.AddInputCharacterUTF16((unsigned short)buffer[j]);
                } else {
                    unsigned short c = VKToAsciiFallback(ev.vk, keyboardState);
                    if (c)
                        io.AddInputCharacterUTF16(c);
                }
            }
        }
    } else {
        memset(hookHandled, 0, sizeof(hookHandled));
    }

    // --- (2) Polling (always runs) ---
    // g_keys[] is rebuilt from the current async state every frame so it can
    // never get stuck on a stale press (key-ups during a closed menu are not
    // seen by the hook, so the hook must not be the source of truth here).
    for (int i = 0; i < 256; i++) {
        // Ignore F13-F24
        if (i >= VK_F13 && i <= VK_F24)
            continue;

        bool down = (GetAsyncKeyState(i) & 0x8000) != 0;

        g_keysPressed[i]  = down && !g_keys[i];
        g_keysReleased[i] = !down && g_keys[i];
        g_keys[i]         = down;

        if (hookHandled[i])
            continue;

        // Send special keys to ImGui
        ImGuiKey imgui_key = VKToImGuiKey(i);
        if (imgui_key != ImGuiKey_None) {
            if (g_keysPressed[i])
                io.AddKeyEvent(imgui_key, true);
            if (g_keysReleased[i])
                io.AddKeyEvent(imgui_key, false);
        }
    }

    // Update modifiers
    io.KeyCtrl  = g_keys[VK_CONTROL];
    io.KeyShift = g_keys[VK_SHIFT];
    io.KeyAlt   = g_keys[VK_MENU];
    io.KeySuper = g_keys[VK_LWIN] || g_keys[VK_RWIN];

    // --- (3) Text characters (fallback for games with no WM_CHAR) ---
    // One character per polling key-edge, using the keyboard layout plus a
    // guaranteed US ASCII fallback for letters/digits/space. Runs whenever the
    // menu is open OR an ImGui text box is focused, so a wrong menuOpen cannot
    // kill character input.
    if ((menuOpen || io.WantTextInput) && !g_wndProcDeliversChars) {
        BYTE keyboardState[256] = {};
        BuildKeyboardState(keyboardState);

        WCHAR buffer[4];
        HKL layout = GetActiveKeyboardLayout();

        for (int vk = 0; vk < 256; vk++) {
            if (vk >= VK_F13 && vk <= VK_F24)
                continue;
            if (hookHandled[vk])
                continue;

            if (g_keysPressed[vk]) {
                UINT scanCode = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
                int result = ToUnicodeEx(vk, scanCode, keyboardState, buffer, 4, 0, layout);

                if (result > 0) {
                    for (int i = 0; i < result; i++)
                        io.AddInputCharacterUTF16((unsigned short)buffer[i]);
                } else {
                    unsigned short c = VKToAsciiFallback(vk, keyboardState);
                    if (c)
                        io.AddInputCharacterUTF16(c);
                }
            }
        }
    }
}

void Input::UpdateMouse(HWND window, float screenWidth, float screenHeight, bool drawCursor) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(screenWidth, screenHeight);

    // Manual input handling - UWP compatible
    POINT p;
    GetCursorPos(&p);
    ScreenToClient(window, &p);
    io.MousePos = ImVec2((float)p.x, (float)p.y);

    // Mouse buttons - direct clean read
    g_prevLmbPressed = g_lmbPressed;
    g_prevRmbPressed = g_rmbPressed;
    
    g_lmbPressed = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    g_rmbPressed = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    
    io.MouseDown[0] = g_lmbPressed;
    io.MouseDown[1] = g_rmbPressed;
    io.MouseDown[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

    // ImGui cursor ONLY drawn when menu open
    io.MouseDrawCursor = drawCursor;
}

bool Input::IsLMBPressed() {
    return g_lmbPressed;
}

bool Input::IsRMBPressed() {
    return g_rmbPressed;
}

bool Input::WasLMBPressed() {
    return g_prevLmbPressed;
}

bool Input::WasRMBPressed() {
    return g_prevRmbPressed;
}

void Input::Update(HWND window, float screenWidth, float screenHeight, bool menuOpen, bool drawCursor) {
    UpdateKeyboard(menuOpen);
    UpdateMouse(window, screenWidth, screenHeight, drawCursor);
}

// Static member definitions for input blocking
HHOOK Input::g_keyboardHook = nullptr;

// Low-level keyboard hook callback: only captures key down/up events while the
// menu is open. No ToUnicodeEx() here (it can fail on this worker thread) and no
// blocking -- the game keeps receiving every key, like the original clickgui.
static LRESULT CALLBACK InputKeyboardBlockHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode != HC_ACTION)
        return CallNextHookEx(Input::g_keyboardHook, nCode, wParam, lParam);

    PKBDLLHOOKSTRUCT pKey = (PKBDLLHOOKSTRUCT)lParam;
    if (!pKey)
        return CallNextHookEx(Input::g_keyboardHook, nCode, wParam, lParam);

    if (g_showMenu) {
        const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        const bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

        if (isDown || isUp) {
            // Scan code with the extended-key bit, as ToUnicodeEx() expects.
            UINT scanCode = (pKey->scanCode & 0xFF) | ((pKey->flags & LLKHF_EXTENDED) ? 0x100 : 0);

            InputEvent ev;
            ev.vk = (int)pKey->vkCode;
            ev.scan = scanCode;
            ev.down = isDown;

            EnterCriticalSection(&g_inputLock);
            g_inputEvents.push_back(ev);
            LeaveCriticalSection(&g_inputLock);
        }
    }

    return CallNextHookEx(Input::g_keyboardHook, nCode, wParam, lParam);
}

// Dedicated thread that installs the hook and pumps messages. WH_KEYBOARD_LL
// callbacks are only dispatched while the installing thread runs a message loop.
static DWORD WINAPI InputKeyboardHookThread(LPVOID lpParam) {
    HMODULE hMod = GetModuleHandleA("aegledll");
    if (!hMod) hMod = GetModuleHandleA(nullptr);

    Input::g_keyboardHook = SetWindowsHookExA(WH_KEYBOARD_LL, InputKeyboardBlockHookProc, hMod, 0);
    if (!Input::g_keyboardHook)
        return 0;

    InterlockedExchange(&g_keyboardHookActive, 1);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (Input::g_keyboardHook) {
        UnhookWindowsHookEx(Input::g_keyboardHook);
        Input::g_keyboardHook = NULL;
    }
    InterlockedExchange(&g_keyboardHookActive, 0);
    return 0;
}

void Input::BlockGameInput() {
    // Start the persistent keyboard hook thread on first use. It stays installed
    // for the process lifetime; it only captures while the menu is open.
    EnsureInputLock();
    if (!g_keyboardHookThread) {
        HANDLE hThread = CreateThread(NULL, 0, InputKeyboardHookThread, NULL, 0, &g_keyboardHookThreadId);
        if (hThread) {
            g_keyboardHookThread = hThread;
        }
    }
}

void Input::UnblockGameInput() {
    // The keyboard hook stays installed; the callback decides based on g_showMenu.
}

void Input::StopKeyboardHook() {
    if (g_keyboardHookThread) {
        PostThreadMessageW(g_keyboardHookThreadId, WM_QUIT, 0, 0);
        WaitForSingleObject(g_keyboardHookThread, 1000);
        CloseHandle(g_keyboardHookThread);
        g_keyboardHookThread = NULL;
    }
}

// -----------------------------------------------------------------------------

bool Input::IsWindowsCursorVisible() {
    CURSORINFO ci = { 0 };
    ci.cbSize = sizeof(CURSORINFO);
    if (GetCursorInfo(&ci)) {
        return (ci.flags & CURSOR_SHOWING) != 0;
    }
    return false;
}

bool Input::IsInWorld() {
    return !IsWindowsCursorVisible();
}

// Debug log appended to the app's RoamingState folder (the only user-writable
// location for an AppContainer): %LOCALAPPDATA%\Packages\<PackageFamilyName>\RoamingState\aegle_cursor.log
static std::wstring GetRoamingStateDir() {
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)))
        return L"";
    WCHAR packageFamily[PACKAGE_FAMILY_NAME_MAX_LENGTH] = {0};
    UINT32 length = PACKAGE_FAMILY_NAME_MAX_LENGTH;
    std::wstring result;
    if (GetPackageFamilyName(GetCurrentProcess(), &length, packageFamily) == ERROR_SUCCESS) {
        result = std::wstring(localAppData) + L"\\Packages\\" + packageFamily + L"\\RoamingState";
        CreateDirectoryW(result.c_str(), NULL);
    }
    CoTaskMemFree(localAppData);
    return result;
}

static void DebugCursorLog(const char* fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    static const std::wstring s_logPath = GetRoamingStateDir() + L"\\aegle_cursor.log";
    HANDLE hFile = CreateFileW(s_logPath.c_str(), FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return;
    char line[600];
    SYSTEMTIME st;
    GetLocalTime(&st);
    sprintf_s(line, "[%02u:%02u:%02u.%03u] %s\r\n",
              st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buffer);
    DWORD written = 0;
    WriteFile(hFile, line, (DWORD)strlen(line), &written, NULL);
    CloseHandle(hFile);
}

void Input::DebugLogCursorState(const char* tag) {
    CURSORINFO ci;
    ci.cbSize = sizeof(ci);
    bool ok = GetCursorInfo(&ci) != FALSE;
    DebugCursorLog("STATE [%s] g_showMenu=%d g_wasInWorld=%d shown=%s flags=0x%X hCursor=%p",
                   tag, g_showMenu, g_wasInWorld,
                   ok ? ((ci.flags & CURSOR_SHOWING) ? "yes" : "no") : "n/a",
                   ok ? ci.flags : 0, ok ? (void*)ci.hCursor : nullptr);
}

LRESULT CALLBACK Input::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Feed every message to the backend exactly like the original dllmain did.
    // The WH_KEYBOARD_LL hook is an additional capture layer, not a replacement:
    // if it is silent, WM_CHAR/WM_KEYDOWN still reach ImGui through this handler.
    if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam))
        return true;

    if (uMsg == WM_CHAR && wParam >= 32 && wParam != 127 && wParam < 0x10000) {
        // The game delivers printable text characters through the message queue,
        // so the other sources in UpdateKeyboard() must not add them again.
        // Garbage WM_CHAR (0 / control codes) is ignored so it can't disable
        // the fallback sources.
        g_wndProcDeliversChars = true;
    }

    if (uMsg == WM_ACTIVATEAPP && wParam == TRUE) {
        // Force audio engine start when game regains focus
        Info::OnFocusGained();
    }

    if (uMsg == WM_INPUT) {
        UINT dwSize = 0;
        GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER));
        if (dwSize > 0) {
            std::vector<BYTE> lpb(dwSize);
            if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb.data(), &dwSize, sizeof(RAWINPUTHEADER)) == dwSize) {
                RAWINPUT* raw = (RAWINPUT*)lpb.data();
                if (raw->header.dwType == RIM_TYPEMOUSE) {
                    MouseStrokes::OnRawMouseInput(raw->data.mouse.lLastX, raw->data.mouse.lLastY);
                }
            }
        }
    }

    if (g_showMenu) {
        switch (uMsg) {
            case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP:
            case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_MBUTTONDOWN:
            case WM_MBUTTONUP: case WM_MOUSEWHEEL: case WM_INPUT: 
                return 1;
        }
    }
    return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
}
