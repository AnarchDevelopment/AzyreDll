#include "KeyGateHook.hpp"

#include "Framework/Hook.hpp"
#include "Framework/Log.hpp"

#include <windows.h>

#include <atomic>

namespace mc::keygate {

namespace {

// El pump del juego corre en su hilo de UI; el panel corre en el de render.
// Todo el estado se consulta con atomicos: el hook no puede bloquearse.
std::atomic<bool> g_armed{false};
std::atomic<bool> g_tBlockArmed{false};
std::atomic<uint64_t> g_grace{0};
std::atomic<bool> g_installed{false};
std::atomic<uint32_t> g_blocked{0};

using PeekFn = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
using GetFn = int(WINAPI*)(LPMSG, HWND, UINT, UINT);

PeekFn g_origPeek = nullptr;
GetFn g_origGet = nullptr;
void* g_peekTarget = nullptr;
void* g_getTarget = nullptr;

bool ownsInput()
{
    if (g_armed.load(std::memory_order_relaxed))
        return true;

    const uint64_t grace = g_grace.load(std::memory_order_relaxed);
    return grace != 0 && GetTickCount64() < grace;
}

void gateMessage(LPMSG msg)
{
    if (!msg)
        return;

    if (g_tBlockArmed.load(std::memory_order_relaxed) &&
        (msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN) &&
        msg->wParam == 'T')
    {
        msg->message = WM_NULL;
        msg->wParam = 0;
        msg->lParam = 0;
        return;
    }

    if (!ownsInput())
        return;

    switch (msg->message)
    {
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
    case WM_UNICHAR:
        msg->message = WM_NULL;
        msg->wParam = 0;
        msg->lParam = 0;
        if (g_blocked.fetch_add(1, std::memory_order_relaxed) == 0)
            MC_LOG("[KeyGate] game keyboard input neutralized");
        break;

    default:
        break;
    }
}

BOOL WINAPI hkPeekMessage(LPMSG msg, HWND hwnd, UINT filterMin, UINT filterMax, UINT remove)
{
    const BOOL result = g_origPeek ? g_origPeek(msg, hwnd, filterMin, filterMax, remove) : FALSE;
    if (result)
        gateMessage(msg);
    return result;
}

int WINAPI hkGetMessage(LPMSG msg, HWND hwnd, UINT filterMin, UINT filterMax)
{
    const int result = g_origGet ? g_origGet(msg, hwnd, filterMin, filterMax) : 0;
    if (result == 1)
        gateMessage(msg);
    return result;
}

void* moduleFunction(const wchar_t* module, const char* name)
{
    HMODULE handle = GetModuleHandleW(module);
    if (!handle)
        handle = LoadLibraryW(module);
    return handle ? reinterpret_cast<void*>(GetProcAddress(handle, name)) : nullptr;
}

} // namespace

bool install()
{
    if (g_installed.load())
        return true;

    // win32u es la capa por la que pasa el pump del stack UWP; user32 queda
    // como respaldo si el export no existe en esta build.
    void* peek = moduleFunction(L"win32u.dll", "NtUserPeekMessage");
    void* get = moduleFunction(L"win32u.dll", "NtUserGetMessage");
    if (!peek)
        peek = moduleFunction(L"user32.dll", "PeekMessageW");
    if (!get)
        get = moduleFunction(L"user32.dll", "GetMessageW");

    bool ok = false;

    if (peek && hook::create(peek, reinterpret_cast<void*>(&hkPeekMessage),
                             reinterpret_cast<void**>(&g_origPeek)))
    {
        g_peekTarget = peek;
        ok = true;
    }

    if (get && hook::create(get, reinterpret_cast<void*>(&hkGetMessage),
                            reinterpret_cast<void**>(&g_origGet)))
    {
        g_getTarget = get;
        ok = true;
    }

    g_installed.store(ok, std::memory_order_relaxed);

    if (ok)
        MC_LOG("[KeyGate] message pump hooks active");
    else
        MC_LOG_ERROR("[KeyGate] message pump hooks failed");

    return ok;
}

void uninstall()
{
    g_armed.store(false, std::memory_order_relaxed);
    g_tBlockArmed.store(false, std::memory_order_relaxed);
    g_grace.store(0, std::memory_order_relaxed);

    if (g_peekTarget)
    {
        hook::remove(g_peekTarget);
        g_peekTarget = nullptr;
        g_origPeek = nullptr;
    }

    if (g_getTarget)
    {
        hook::remove(g_getTarget);
        g_getTarget = nullptr;
        g_origGet = nullptr;
    }

    g_installed.store(false, std::memory_order_relaxed);
}

void setArmed(bool armed)
{
    if (armed)
    {
        g_grace.store(0, std::memory_order_relaxed);
        g_armed.store(true, std::memory_order_relaxed);
        return;
    }

    if (g_armed.exchange(false, std::memory_order_relaxed))
        g_grace.store(GetTickCount64() + 250ull, std::memory_order_relaxed);
}

void setTBlockArmed(bool armed)
{
    g_tBlockArmed.store(armed, std::memory_order_relaxed);
}

bool shouldBlockT()
{
    return g_tBlockArmed.load(std::memory_order_relaxed);
}

bool shouldCaptureKeyboard()
{
    return ownsInput();
}

bool installed()
{
    return g_installed.load();
}

}
