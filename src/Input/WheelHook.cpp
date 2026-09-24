#include "WheelHook.hpp"

#include "Framework/Hook.hpp"
#include "Framework/Log.hpp"
#include "GUI/Menu.hpp"

#include <MinHook.h>
#include <atomic>

namespace mc::wheel {

static HMODULE g_module = nullptr;
static HHOOK g_hook = nullptr;
static HANDLE g_thread = nullptr;
static DWORD g_threadId = 0;
static std::atomic<int> g_llUnits{0};
static std::atomic<int> g_winrtUnits{0};
static bool g_loggedWinrt = false;
static bool g_loggedLL = false;
static float g_lastValue = 0.0f;

static bool g_msgHooked = false;
static LPVOID g_peekTarget = nullptr;
static LPVOID g_getTarget = nullptr;
static decltype(&PeekMessageW) oPeek = nullptr;
static decltype(&GetMessageW) oGet = nullptr;

void setModule(void* moduleHandle)
{
    g_module = static_cast<HMODULE>(moduleHandle);
}

float lastValue()
{
    return g_lastValue;
}

void addWinrtDelta(int wheelDelta)
{
    if (wheelDelta == 0)
        return;
    g_winrtUnits.fetch_add(wheelDelta);
    if (!g_loggedWinrt)
    {
        g_loggedWinrt = true;
        MC_LOG("[Wheel] WinRT source active");
    }
}

float consume()
{
    int winrtUnits = g_winrtUnits.exchange(0);
    int llUnits = g_llUnits.exchange(0);

    float value = 0.0f;
    if (winrtUnits != 0)
        value = (float)winrtUnits / 120.0f;
    else if (llUnits != 0)
    {
        value = (float)llUnits / 120.0f;
        if (!g_loggedLL)
        {
            g_loggedLL = true;
            MC_LOG("[Wheel] WH_MOUSE_LL source active");
        }
    }

    if (value != 0.0f)
        g_lastValue = value;
    return value;
}

static void feedWheel(int delta)
{
    if (delta == 0)
        return;
    g_llUnits.fetch_add(delta);
}

static LRESULT CALLBACK LLMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && wParam == WM_MOUSEWHEEL)
    {
        const MSLLHOOKSTRUCT* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        int delta = (short)HIWORD(info->mouseData);
        if (delta != 0)
        {
            feedWheel(delta);
            if (mc::menu::visible())
                return 1;
        }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

static BOOL WINAPI hkPeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax,
                                  UINT wRemoveMsg)
{
    BOOL r = oPeek(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    if (r && lpMsg && lpMsg->message == WM_MOUSEWHEEL && (wRemoveMsg & PM_REMOVE))
    {
        int delta = (short)HIWORD(lpMsg->wParam);
        feedWheel(delta);
        if (mc::menu::visible())
            lpMsg->message = WM_NULL;
    }
    return r;
}

static BOOL WINAPI hkGetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    BOOL r = oGet(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
    if (r && lpMsg && lpMsg->message == WM_MOUSEWHEEL)
    {
        int delta = (short)HIWORD(lpMsg->wParam);
        feedWheel(delta);
        if (mc::menu::visible())
            lpMsg->message = WM_NULL;
    }
    return r;
}

static bool installMessageHooks()
{
    if (g_msgHooked)
        return true;
    if (!hook::initialize())
    {
        MC_LOG_ERROR("[Wheel] MinHook init failed");
        return false;
    }

    MH_STATUS s1 = MH_CreateHookApiEx(L"user32.dll", "PeekMessageW",
                                      reinterpret_cast<LPVOID>(&hkPeekMessageW),
                                      reinterpret_cast<LPVOID*>(&oPeek), &g_peekTarget);
    MH_STATUS s2 = MH_CreateHookApiEx(L"user32.dll", "GetMessageW",
                                      reinterpret_cast<LPVOID>(&hkGetMessageW),
                                      reinterpret_cast<LPVOID*>(&oGet), &g_getTarget);

    if (s1 == MH_OK && g_peekTarget)
        MH_EnableHook(g_peekTarget);
    if (s2 == MH_OK && g_getTarget)
        MH_EnableHook(g_getTarget);

    g_msgHooked = (s1 == MH_OK) || (s2 == MH_OK);
    if (g_msgHooked)
        MC_LOG("[Wheel] PeekMessageW/GetMessageW hooks active (Peek=%d Get=%d)", (int)s1, (int)s2);
    else
        MC_LOG_ERROR("[Wheel] Message hooks failed (Peek=%d Get=%d)", (int)s1, (int)s2);
    return g_msgHooked;
}

static DWORD WINAPI WheelThread(LPVOID)
{
    g_hook = SetWindowsHookExW(WH_MOUSE_LL, LLMouseProc, g_module, 0);
    if (!g_hook)
    {
        MC_LOG_ERROR("[Wheel] SetWindowsHookEx failed: %lu -> trying PeekMessageW", GetLastError());
        installMessageHooks();
        return 0;
    }

    MC_LOG("[Wheel] WH_MOUSE_LL installed");

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hook)
    {
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }
    MC_LOG("[Wheel] Hook removed");
    return 0;
}

bool install()
{
    MC_LOG("[Wheel] installing (module=%p)", g_module);

    if (g_thread || g_msgHooked)
        return true;
    if (!g_module)
    {
        MC_LOG_ERROR("[Wheel] Module handle not set");
        return false;
    }

    g_thread = CreateThread(nullptr, 0, WheelThread, nullptr, 0, &g_threadId);
    if (!g_thread)
    {
        MC_LOG_ERROR("[Wheel] CreateThread failed: %lu", GetLastError());
        installMessageHooks();
        return false;
    }

    MC_LOG("[Wheel] hook thread started (id=%lu)", g_threadId);
    return true;
}

void uninstall()
{
    if (g_msgHooked)
    {
        if (g_peekTarget)
            MH_DisableHook(g_peekTarget);
        if (g_getTarget)
            MH_DisableHook(g_getTarget);
        g_msgHooked = false;
    }

    if (g_thread)
    {
        PostThreadMessageW(g_threadId, WM_QUIT, 0, 0);
        WaitForSingleObject(g_thread, 1000);
        CloseHandle(g_thread);
        g_thread = nullptr;
        g_threadId = 0;
    }
    g_llUnits.store(0);
    g_winrtUnits.store(0);
}

}
